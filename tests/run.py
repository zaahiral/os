#!/usr/bin/env python3
"""Build and run the numbered README tests in isolated QEMU guests."""

import argparse
from collections import Counter
from contextlib import contextmanager
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

from packets import arp_exchange, checksum, ipv4_packets, read_pcap, require, tcp_packets

ROOT = Path(__file__).resolve().parent.parent
KEYS = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', '=': 'equal',
        ',': 'comma', ';': 'semicolon', "'": 'apostrophe', '(': 'shift-9',
        ')': 'shift-0', '*': 'shift-8', '"': 'shift-apostrophe', '+': 'shift-equal',
        '_': 'shift-minus', '<': 'shift-comma', '>': 'shift-dot', '!': 'shift-1',
        ':': 'shift-semicolon', '%': 'shift-5', '{': 'shift-bracket_left',
        '}': 'shift-bracket_right', '[': 'bracket_left', ']': 'bracket_right'}


class Guest:
    def __init__(self, suite, name, disk=None, expected=None, udp=False):
        self.suite = suite
        self.directory = suite.output / name
        self.directory.mkdir(parents=True, exist_ok=True)
        self.expected = expected or {}
        self.monitor = None
        self.process = None
        self.transcript = []
        self.screens = []
        self.disk = disk or self.directory / 'disk.img'
        if disk is None:
            shutil.copy2(suite.build / 'disk.img', self.disk)
        self.udpPort = None
        network = 'user,id=net'
        if udp:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
                reservation.bind(('127.0.0.1', 0))
                self.udpPort = reservation.getsockname()[1]
            network += ',hostfwd=udp:127.0.0.1:' + str(self.udpPort) + '-:1234'
        command = [suite.qemu, '-cdrom', str(suite.build / 'cOS.iso'), '-m', '1G',
                   '-display', 'none', '-no-reboot', '-no-shutdown', '-boot', 'd',
                   '-drive', 'file=' + str(self.disk) + ',format=raw,if=ide,index=0,media=disk',
                   '-netdev', network, '-device', 'pcnet,netdev=net',
                   '-object', 'filter-dump,id=capture,netdev=net,file=' + str(self.directory / 'network.pcap'),
                   '-monitor', 'tcp:127.0.0.1:' + str(suite.port) + ',server=on,wait=off',
                   '-d', 'int', '-D', str(self.directory / 'qemu.log')]
        self.stderr = (self.directory / 'qemu.stderr').open('w')
        self.process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=self.stderr)
        deadline = time.monotonic() + 10
        try:
            while self.monitor is None:
                require(self.process.poll() is None, 'QEMU exited: ' + (self.directory / 'qemu.stderr').read_text())
                try:
                    self.monitor = socket.create_connection(('127.0.0.1', suite.port), timeout=0.2)
                except OSError:
                    require(time.monotonic() < deadline, 'QEMU monitor did not start')
                    time.sleep(0.05)
            self.monitor.settimeout(5)
            self._response()
        except BaseException:
            self.close()
            raise

    def _response(self):
        data = b''
        while not data.endswith(b'(qemu) '):
            block = self.monitor.recv(65536)
            require(block, 'QEMU monitor disconnected')
            data += block
        return data.decode(errors='replace')

    def command(self, command):
        self.monitor.sendall((command + '\n').encode())
        result = self._response().split('\r\n', 1)[-1].replace('\r', '')
        self.transcript.append(command + '\n' + result)
        return result

    def screen(self):
        dump = self.command('xp /4000bx 0xb8000')
        values = []
        for line in dump.splitlines():
            match = re.match(r'^[0-9a-fA-F]+:\s+(.*)', line.strip())
            if match:
                values.extend(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})\b', match[1]))
        require(len(values) == 4000, 'Could not read the VGA text buffer')
        chars = bytes(values[::2]).decode('cp437').replace('\x00', ' ')
        screen = '\n'.join(chars[i:i + 80].rstrip() for i in range(0, 2000, 80)).rstrip()
        if not self.screens or self.screens[-1] != screen:
            self.screens.append(screen)
        return screen

    def wait(self, predicate, description, timeout=20):
        deadline = time.monotonic() + timeout * self.suite.timeoutScale
        while True:
            screen = self.screen()
            if predicate(screen):
                return screen
            require(self.process.poll() is None, 'QEMU exited while waiting for ' + description)
            if time.monotonic() >= deadline:
                raise AssertionError('Timed out waiting for ' + description + '\n' + screen)
            time.sleep(0.1)

    def expect(self, text, timeout=20):
        return self.wait(lambda screen: text.rstrip() in screen, repr(text), timeout)

    def key(self, name):
        self.command('sendkey ' + name + ' 1')
        time.sleep(0.025 * self.suite.timeoutScale)

