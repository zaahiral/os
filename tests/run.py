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

    def type(self, text, enter=True):
        for character in text:
            name = 'shift-' + character.lower() if character.isupper() else KEYS.get(character, character)
            self.key(name)
        if enter:
            self.key('ret')

    def shell(self):
        self.expect('$ ')
        self.type('clear')
        self.wait(lambda screen: screen.strip() == '$', 'a cleared user shell')

    def kernel_shell(self):
        self.shell()
        self.type('exit')
        self.expect('cOS> ')
        self.type('clear')
        self.wait(lambda screen: screen.strip() == 'cOS>', 'a cleared kernel shell')

    def run(self, command, expected, timeout=20):
        before = self.screen()
        self.type(command)
        return self.wait(lambda screen: screen != before and expected.rstrip() in screen
                         and (command == 'run initrd/scripts/countdown.cs'
                              or screen.endswith('$') or screen.endswith('cOS>')),
                         repr(expected) + ' after ' + command, timeout)

    def screenshot(self, name='screen'):
        path = self.directory / (name + '.ppm')
        result = self.command('screendump "' + str(path) + '"')
        require(path.exists(), 'Screenshot failed: ' + result)
        return path

    def close(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.stderr.close()
        if self.monitor is not None:
            self.monitor.close()
            self.monitor = None
        (self.directory / 'monitor.txt').write_text('\n'.join(self.transcript))
        (self.directory / 'screens.txt').write_text('\n\n---\n\n'.join(self.screens))

    def check_vectors(self):
        log = (self.directory / 'qemu.log').read_text(errors='replace')
        vectors = Counter(int(v, 16) for v in re.findall(r'\bv=([0-9a-fA-F]{2})\b', log))
        (self.directory / 'vectors.json').write_text(json.dumps({f'{v:02x}': n for v, n in sorted(vectors.items())}, indent=2) + '\n')
        exceptions = {v: n for v, n in vectors.items() if v < 32}
        require(exceptions == self.expected,
                'Exception vectors: expected ' + repr(self.expected) + ', got ' + repr(exceptions))
        require('Triple fault' not in log, 'Triple fault')


class Suite:
    def __init__(self, args):
        self.output = args.output.resolve()
        require(self.output != ROOT and self.output not in ROOT.parents,
                'Output must not replace the repository')
        self.output.mkdir(parents=True, exist_ok=True)
        self.source = self.output / 'source'
        self.build = self.output / 'build'
        self.native = args.native_toolchain.resolve() if args.native_toolchain else None
        self.qemu = args.qemu
        self.port = args.port
        self.jobs = args.jobs
        self.timeoutScale = args.timeout_scale
        self.variant = None
        require(self.source != ROOT and self.source not in ROOT.parents, 'Unsafe output directory')
        marker = self.output / '.cos-tests'
        if self.source.exists():
            require(marker.exists(), 'Output contains an unrelated source directory: ' + str(self.source))
            shutil.rmtree(self.source)
        marker.touch()
        self.source.mkdir()
        for name in ('src', 'include', 'userspace', 'targets', 'toolchain'):
            shutil.copytree(ROOT / name, self.source / name)
        shutil.copy2(ROOT / 'CMakeLists.txt', self.source / 'CMakeLists.txt')
        for program in (ROOT / 'tests/programs').glob('*.cpp'):
            shutil.copy2(program, self.source / 'userspace' / program.name)
        initrd = self.source / 'targets/x86_64/initrd'
        (initrd / 'dir').mkdir()
        (initrd / 'afile.txt').write_text('Hello from a test file.\n')
        (initrd / 'dir/afile.txt').write_text('Hello from a test directory.\n')
        self.kernel = (self.source / 'src/kernel/kernel.cpp').read_text()
        self.build_command(['cmake', '-S', str(self.source), '-B', str(self.build)] +
                           (['-DNATIVE_TOOLCHAIN=' + str(self.native)] if self.native else ['-DNATIVE_TOOLCHAIN=']))

    def build_command(self, command):
        with (self.output / 'build.log').open('a') as log:
            log.write('\n' + ' '.join(command) + '\n')
            log.flush()
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        require(result.returncode == 0, 'Build failed. See ' + str(self.output / 'build.log'))

    def build_variant(self, snippet=None):
        if self.variant == (snippet or 'shell'):
            return
        kernel = self.kernel
        if snippet:
            globalsText, body = (ROOT / 'tests/snippets' / snippet).read_text().split('//@body')
            globalsText = globalsText.replace('//@globals', '').strip()
            start = kernel.index('    auto shellTask = Task::TaskManager::instance().create_new_kernel_task();')
            end = kernel.index('    Task::TaskManager::instance().run();\n', start) + len('    Task::TaskManager::instance().run();\n')
            kernel = kernel[:start] + '    clear_screen();\n' + body.strip('\n') + '\n' + kernel[end:]
            if 'void task_one()' in globalsText:
                start = kernel.index('void task_one()')
                end = kernel.index('\n}\n', start) + 3
                kernel = kernel[:start] + kernel[end:]
            start = kernel.index('extern "C" void pre_kernel(')
            kernel = kernel[:start] + globalsText + '\n\n' + kernel[start:]
        (self.source / 'src/kernel/kernel.cpp').write_text(kernel)
        self.build_command(['cmake', '--build', str(self.build), '-j' + str(self.jobs)])
        self.variant = snippet or 'shell'

    @contextmanager
    def boot(self, name, snippet=None, **kwargs):
        self.build_variant(snippet)
        guest = Guest(self, name, **kwargs)
        try:
            yield guest
        finally:
            try:
                guest.screen()
                guest.screenshot()
            finally:
                guest.close()
            guest.check_vectors()


def boot_test(suite):
    with suite.boot('1.1') as guest:
        guest.expect('cOS user shell')
        guest.expect('$ ')


def memory_test(suite):
    with suite.boot('1.2', 't1.2.cpp') as guest:
        screen = guest.expect('MEMORY DETECTED:')
        regions = [(int(length), int(base)) for length, base in
                   re.findall(r'MEMORY DETECTED: (\d+) bytes at (\d+)', screen)]
        require(len(regions) >= 2, 'Missing usable memory regions')
        require(any(base == 0 for length, base in regions), 'Missing low memory')
        end = max(base + length for length, base in regions)
        require(1024 ** 3 - 1024 ** 2 <= end <= 1024 ** 3, 'Memory does not match QEMU 1GiB')


def physical_test(suite):
    with suite.boot('1.3', 't1.3.cpp') as guest:
        screen = guest.expect('Contents of 0x1000:')
        value = int(re.search(r'Contents of 0x1000: (\d+)', screen)[1])
        dump = guest.command('xp /1bx 0x1000')
        physical = re.search(r'0*1000:\s+0x([0-9a-fA-F]+)', dump)
        require(physical and value == int(physical[1], 16), 'Virtual and physical bytes differ')


def table_test(suite, name, register, minimum):
    with suite.boot(name) as guest:
        guest.expect('$ ')
        registers = guest.command('info registers')
        match = re.search(register + r'=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)', registers)
        require(match and int(match[1], 16) >= 0xffff800000000000 and int(match[2], 16) >= minimum,
                'Invalid ' + register + ' register')
        if register == 'GDT':
            require(re.search(r'TR\s*=.*TSS64', registers), 'Missing 64-bit TSS')


def allocation_test(suite):
    with suite.boot('1.5-unmapped', 't1.5a.cpp', expected={14: 1}) as guest:
        guest.expect('PAGE FAULT4096')
    with suite.boot('1.5-mapped', 't1.5b.cpp') as guest:
        guest.wait(lambda text: re.search(r'Value of virtual address 0x1000: \d+', text), 'mapped byte')


def pic_test(suite):
    with suite.boot('1.7') as guest:
        guest.expect('$ ')
        pic = guest.command('info pic')
        require(re.search(r'pic0:.*irq_base=20', pic) and re.search(r'pic1:.*irq_base=28', pic), 'PIC offsets differ')


def keyboard_test(suite):
    with suite.boot('1.8') as guest:
        guest.shell()
        guest.type('keyboard check', enter=False)
        guest.expect('$ keyboard check')
    log = (suite.output / '1.8/qemu.log').read_text()
    require(re.search(r'v=21\b', log), 'No keyboard interrupt')


def fault_test(suite):
    with suite.boot('1.9', 't1.9.cpp', expected={14: 1}) as guest:
        guest.expect('PAGE FAULT4321')


def snippet_test(suite, name, snippet, expected):
    with suite.boot(name, snippet) as guest:
        guest.expect(expected)


def pci_test(suite):
    with suite.boot('2.3') as guest:
        guest.kernel_shell()
        screen = guest.run('pci', 'Vendor:Device')
        kernel = Counter(re.findall(r'\b([0-9A-F]{4}):([0-9A-F]{4})\b', screen))
        monitor = Counter((v.upper(), d.upper()) for v, d in
                          re.findall(r'PCI device ([0-9a-fA-F]{4}):([0-9a-fA-F]{4})', guest.command('info pci')))
        require(kernel and kernel == monitor, 'PCI devices differ: ' + repr(kernel) + ' vs ' + repr(monitor))


def events_test(suite):
    with suite.boot('2.6', 't2.6.cpp') as guest:
        guest.expect('Keyboard listener ready.')
        guest.key('a')
        guest.expect('Scan code received: 30')
        guest.expect('Scan code received: 158')


def disk_read_test(suite):
    with suite.boot('2.7') as guest:
        guest.shell()
        guest.run('ls disk', 'readme.txt')
        guest.run('cat disk/readme.txt', (ROOT / 'targets/x86_64/disk/readme.txt').read_text().strip())

