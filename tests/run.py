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
KEYS = {'@': 'shift-2', ' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', '=': 'equal',
        ',': 'comma', ';': 'semicolon', "'": 'apostrophe', '(': 'shift-9',
        ')': 'shift-0', '*': 'shift-8', '"': 'shift-apostrophe', '+': 'shift-equal',
        '_': 'shift-minus', '<': 'shift-comma', '>': 'shift-dot', '!': 'shift-1',
        ':': 'shift-semicolon', '%': 'shift-5', '{': 'shift-bracket_left',
        '}': 'shift-bracket_right', '[': 'bracket_left', ']': 'bracket_right'}


class Guest:
    def __init__(self, suite, name, disk=None, expected=None, udp=False, iso=None):
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
        command = [suite.qemu, '-cdrom', str(iso or suite.build / 'cOS.iso'), '-m', '1G',
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


def disk_write_test(suite):
    with suite.boot('2.8-write') as guest:
        guest.shell()
        guest.run('write disk/notes.txt hello from the shell', '$ ')
        guest.run('writefile', 'Added a line to disk/log.txt.')
        guest.run('cat disk/notes.txt', 'hello from the shell')
        disk = guest.disk
    with suite.boot('2.8-reboot', disk=disk) as guest:
        guest.shell()
        guest.run('cat disk/notes.txt', 'hello from the shell')
        guest.run('cat disk/log.txt', 'Written by a user-space program.')
    for name, expected in [('notes.txt', b'hello from the shell\n'),
                           ('log.txt', b'Written by a user-space program.\n')]:
        data = subprocess.check_output(['mtype', '-i', str(disk), '::' + name])
        require(data == expected, 'Host disk contents differ for ' + name + ': ' + repr(data))


@contextmanager
def tcp_server(port):
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', port))
    server.listen(1)
    server.settimeout(45)
    received = []
    errors = []

    def serve():
        try:
            connection, _ = server.accept()
            with connection:
                connection.settimeout(20)
                data = b''
                while True:
                    block = connection.recv(4096)
                    if not block:
                        break
                    data += block
                    if data == b'Hello network!' or b'{"test":"data"}' in data:
                        break
                received.append(data)
                # Keep the endpoint open until the guest has captured its final ACK.
                time.sleep(1)
        except OSError as error:
            errors.append(str(error))

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        yield received
        thread.join(timeout=25)
        require(not thread.is_alive() and not errors, 'TCP server failed: ' + repr(errors))
    finally:
        server.close()


def network_test(suite, number):
    name = '3.' + str(number)
    with tcp_server(1234 if number == 7 else 3939) if number in (7, 8) else null_server() as received:
        with suite.boot(name, 'net-' + name + '.cpp', udp=number == 6) as guest:
            guest.expect('Network test ready.')
            if number == 6:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    sender.sendto(b'Hello from the host!', ('127.0.0.1', guest.udpPort))
                guest.expect('Hello from the host!')
            if number in (7, 8):
                deadline = time.monotonic() + 20 * suite.timeoutScale
                while not received and time.monotonic() < deadline:
                    time.sleep(0.1)
                require(received, 'Host did not receive TCP data')
        frames = read_pcap(suite.output / name / 'network.pcap')
        payload = b'Hello network!'
        if number == 1:
            require(frames == [payload], 'Raw transmission differs: ' + repr(frames))
        elif number == 2:
            require(len(frames) == 1 and frames[0][:6] == b'\xff' * 6
                    and frames[0][6:12] == bytes.fromhex('525400123456')
                    and frames[0][12:] == b'\0\0' + payload, 'Ethernet frame differs')
        elif number == 3:
            mac = arp_exchange(frames)
            guestScreen = '\n'.join(guest.screens)
            require('MAC address (big endian): ' + str(int.from_bytes(mac, 'little')) in guestScreen,
                    'Cached MAC differs from ARP reply')
        elif number in (4, 5):
            arp_exchange(frames)
            packets = ipv4_packets(frames, 0 if number == 4 else 17)
            if number == 4:
                require(any(data == payload for _, data in packets), 'IPv4 payload differs')
            else:
                udp = next(data for ip, data in packets if ip[12:16] == bytes([10, 0, 2, 15]))
                source, dest, length, check = struct.unpack('!HHHH', udp[:8])
                require(source > 0 and dest == 1234 and length == len(udp) and udp[8:] == payload,
                        'UDP header/payload differs')
                pseudo = bytes([10, 0, 2, 15, 10, 0, 2, 2, 0, 17]) + struct.pack('!H', length)
                require(check == 0 or checksum(pseudo + udp) == 0, 'Invalid UDP checksum')
        elif number == 6:
            require(any(data[8:] == b'Hello from the host!' for _, data in ipv4_packets(frames, 17)),
                    'Incoming UDP payload differs')
        else:
            packets = tcp_packets(frames)
            sent = b''.join(data for _, _, data in packets)
            require(received[0] in sent, 'Capture does not contain the TCP data received by host')
            if number == 7:
                require(received == [payload], 'TCP payload differs')
            else:
                headers, body = received[0].split(b'\n\n', 1)
                require(headers.startswith(b'POST /d493159e-39a2-4015-a5b7-cfcc05a9d739 HTTP/1.1\n')
                        and b'Host: localhost\n' in headers + b'\n'
                        and b'Content-Length: 15' in headers and body == b'{"test":"data"}', 'HTTP POST differs')


@contextmanager
def null_server():
    yield None


def tasks_test(suite, number):
    with suite.boot('4.' + str(number), 't4.' + str(number) + '.cpp') as guest:
        screen = guest.wait(lambda text: all('Task ' + n in text for n in ('one', 'two', 'three', 'four')), 'four tasks')
        if number == 2:
            require(re.findall(r'Task (\w+)', screen) == ['three', 'two', 'four', 'one'], 'Tasks ran out of priority order')
        if number == 3:
            before = (guest.directory / 'qemu.log').stat().st_size
            guest.key('a')
            time.sleep(0.5 * suite.timeoutScale)
            log = (guest.directory / 'qemu.log').read_text()[before:]
            require('v=20' in log and 'v=21' in log, 'Blocked tasks stopped hardware interrupts')


def privileged_test(suite):
    with suite.boot('4.5', expected={13: 1}) as guest:
        guest.shell()
        guest.run('privileged', 'The program was stopped by exception 13.')
        guest.run('echo still running', '\nstill running\n$')
        require('The privileged instruction returned.' not in '\n'.join(guest.screens), 'cli returned in user space')
    log = (suite.output / '4.5/qemu.log').read_text()
    require(re.search(r'v=0d .*cpl=3', log), 'GP fault did not originate in ring 3')


def sleep_test(suite):
    with suite.boot('4.6', 'boot.cpp') as guest:
        guest.expect('Task one.')
        samples = []
        deadline = time.monotonic() + 4.5 * suite.timeoutScale
        last = 0
        while time.monotonic() < deadline:
            count = guest.screen().count('Task one.')
            if count != last:
                samples.append((time.monotonic(), count))
                last = count
            time.sleep(0.05)
        intervals = [right[0] - left[0] for left, right in zip(samples, samples[1:])]
        (guest.directory / 'timing.json').write_text(json.dumps(intervals) + '\n')
        require(len(intervals) >= 3 and all(0.6 <= interval <= 1.6 * suite.timeoutScale for interval in intervals),
                'sleep_for did not print roughly once per second: ' + repr(intervals))


def reclaim_test(suite):
    with suite.boot('4.8') as guest:
        guest.shell()
        guest.run('hello', 'Hello from userspace!')
        guest.run('launch', 'readfile exited with code 27.')
        guest.run('readfile', 'Exited with code 27.')
        guest.type('exit')
        guest.expect('cOS> ')
        guest.run('exec memtest', '0 corrupt blocks.')
        screen = guest.run('mem', 'Free memory:')
        before = int(re.findall(r'Free memory: (\d+) KiB', screen)[-1])
        samples = [before]
        for _ in range(10):
            guest.type('clear')
            guest.wait(lambda text: text.strip() == 'cOS>', 'a cleared kernel shell')
            guest.run('exec memtest', 'Exited with code 0.')
            screen = guest.run('mem', 'Free memory:')
            samples.append(int(re.findall(r'Free memory: (\d+) KiB', screen)[-1]))
        after = samples[-1]
        (guest.directory / 'memory.json').write_text(json.dumps({'beforeKiB': before, 'afterKiB': after, 'samplesKiB': samples}) + '\n')
        # The kernel heap retains its backing pages, so allow one page of allocator growth.
        require(before - after <= 4, 'Memory fell after ten runs: ' + str(before - after) + ' KiB')


def userspace_memory_test(suite):
    with suite.boot('4.9', expected={14: 1}) as guest:
        guest.shell()
        guest.run('memtest', 'Allocated 100 blocks, reused 50 freed blocks, and found 0 corrupt blocks.')
        guest.expect('with 0 non-zero words, and unmapped it.')
        guest.run('crash', 'The program was stopped by a page fault at address 16.')
        guest.run('echo still running', '\nstill running\n$')
        require('This line is never printed.' not in '\n'.join(guest.screens), 'Faulting process continued')


def arguments_test(suite):
    with suite.boot('4.10') as guest:
        guest.shell()
        guest.run('ls initrd/scripts disk', 'readme.txt')
        guest.expect('fibonacci.cs')
        guest.run('cat initrd/hello.txt disk/readme.txt', 'This file is on the FAT32 disk.')
        guest.expect('Hello from the cOS initrd!')
        paths = ['initrd/hello.txt', 'disk/readme.txt', 'initrd/scripts/fibonacci.cs']
        counts = []
        for path in paths:
            data = (ROOT / 'targets/x86_64' / path).read_bytes()
            counts.append((data.count(b'\n'), len(data.split()), len(data)))
        counts.append(tuple(sum(row[i] for row in counts) for i in range(3)))
        screen = guest.run('wc ' + ' '.join(paths), ' total')
        actual = [tuple(map(int, values)) for values in re.findall(r'^\s*(\d+)\s+(\d+)\s+(\d+) ', screen, re.M)]
        require(actual == counts, 'wc counts differ: ' + repr(actual) + ' vs ' + repr(counts))


def compiler_test(suite):
    with suite.boot('4.11') as guest:
        guest.shell()
        guest.run('gcc disk/hello.c -o disk/hello', '\n$ ', timeout=180)
        guest.run('disk/hello one two', 'Hello from a program compiled on cOS, with 3 arguments.', timeout=30)


def cxx_test(suite):
    suite.build_variant()
    disk = suite.output / 'cxx-disk.img'
    shutil.copy2(suite.build / 'disk.img', disk)
    subprocess.run(['mcopy', '-i', str(disk), str(ROOT / 'src/x86_64/task/task.asm'), '::task.asm'], check=True)
    with suite.boot('4.12', disk=disk) as guest:
        guest.shell()
        guest.run('g++ @disk/cxx.rsp', '\n$ ', timeout=180)
        guest.run('disk/cxx', 'Hello from C++ compiled on cOS!')
        guest.run('nasm -f elf64 disk/task.asm -o disk/task.o', '\n$ ', timeout=60)
    obj = subprocess.check_output(['mtype', '-i', str(disk), '::task.o'])
    require(obj[:5] == b'\x7fELF\x02' and struct.unpack_from('<H', obj, 16)[0] == 1,
            'NASM did not produce an ELF64 relocatable object')


def selfhost_test(suite):
    suite.build_variant()
    suite.build_command(['cmake', '--build', str(suite.build), '--target', 'selfhost-disk'])
    directory = suite.output / '4.13'
    directory.mkdir(exist_ok=True)
    disk = directory / 'disk.img'
    shutil.copy2(suite.build / 'selfhost.img', disk)
    count = len((suite.build / 'selfhost-files/build.txt').read_text().splitlines())
    with suite.boot('4.13', disk=disk) as guest:
        guest.shell()
        guest.run('gcc disk/build.c -o disk/build', '\n$ ', timeout=180)
        screen = guest.run('disk/build disk/build.txt', '\n$ ', timeout=1800)
        require('Build complete: ' + str(count) + ' commands.' in screen, 'Native kernel build failed:\n' + screen)
    suite.build_command(['cmake', '-S', str(suite.source), '-B', str(suite.build),
                         '-DSELFHOST_BUILT_DISK=' + str(disk)])
    suite.build_command(['cmake', '--build', str(suite.build), '--target', 'selfhost-iso'])
    shutil.copy2(suite.build / 'kernel-selfhost.bin', directory / 'kernel.bin')
    iso = suite.build / 'selfhost.iso'
    with suite.boot('4.13-reboot', disk=disk, iso=iso) as guest:
        guest.expect('$ ')
        guest.run('echo self-hosted kernel', 'self-hosted kernel')


def shell_test(suite):
    with suite.boot('5.1') as guest:
        guest.shell()
        guest.run('ls initrd', 'hello.txt')
        guest.run('ls initrd/scripts', 'fibonacci.cs')
        guest.run('cat initrd/hello.txt', 'Hello from the cOS initrd!')


def gui_test(suite):
    with suite.boot('5.2') as guest:
        guest.kernel_shell()
        guest.run('echo text survives graphics', '\ntext survives graphics\ncOS>')
        guest.type('gui')
        time.sleep(1 * suite.timeoutScale)
        data = guest.screenshot('desktop').read_bytes()
        match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
        require(match and match.groups() == (b'1024', b'768'), 'Desktop did not enter 1024x768 graphics')
        pixels = data[match.end():]
        for x, y, colour in [(10, 10, '111111'), (10, 700, '2e3236'),
                              (61, 71, 'e9e9e9'), (65, 260, 'ffffff'), (60, 70, '5a5a5a')]:
            offset = (y * 1024 + x) * 3
            require(pixels[offset:offset + 3] == bytes.fromhex(colour), 'Desktop pixel differs at ' + str((x, y)))
        guest.key('esc')
        guest.expect('text survives graphics')
        guest.run('echo returned', '\nreturned\ncOS>')


def script_test(suite, number):
    with suite.boot('5.' + str(number)) as guest:
        guest.kernel_shell()
        if number == 3:
            guest.run('run initrd/scripts/hello.cs', 'Hello from cOS!')
            guest.expect('6 * 7 = 42')
            guest.wait(lambda text: re.search(r'The kernel has been running for \d+ seconds\.', text), 'uptime builtin')
        elif number == 4:
            screen = guest.run('run initrd/scripts/fibonacci.cs', 'fibonacci(14) = 377')
            values = [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377]
            require(re.findall(r'fibonacci\((\d+)\) = (\d+)', screen) ==
                    [(str(i), str(value)) for i, value in enumerate(values)], 'Fibonacci output differs')
        elif number == 5:
            guest.run('run initrd/scripts/error.cs', 'Error on line 4:')
            guest.run('cs print(missing);', 'Error on line 1: undefined variable missing.')
            guest.run('cs print(1 / 0);', 'Error on line 1: division by zero.')
            guest.run('echo still running', '\nstill running\ncOS>')
            require('This line is never run.' not in '\n'.join(guest.screens), 'Script continued after error')
        else:
            guest.run('run initrd/scripts/countdown.cs', '10...')
            guest.expect('9...')
            guest.key('esc')
            guest.expect('cOS> ')
            guest.run('echo stopped', '\nstopped\ncOS>')
            require('Lift off!' not in '\n'.join(guest.screens), 'Countdown was not interrupted')


def restart_shell_test(suite):
    with suite.boot('5.7') as guest:
        guest.shell()
        screen = guest.run('ls initrd/bin', 'sh')
        require(re.search(r'^sh\s*$', screen, re.M), 'sh is not listed')
        guest.run('exit', 'This is the kernel shell.')
        guest.run('exec sh', 'cOS user shell')
        guest.run('echo restarted', '\nrestarted\n$')


def cases():
    return [
        ('1.1', boot_test), ('1.2', memory_test), ('1.3', physical_test),
        ('1.4', lambda s: table_test(s, '1.4', 'GDT', 0x37)), ('1.5', allocation_test),
        ('1.6', lambda s: table_test(s, '1.6', 'IDT', 0xfff)), ('1.7', pic_test),
        ('1.8', keyboard_test), ('1.9', fault_test),
        ('2.1', lambda s: snippet_test(s, '2.1', 't2.1.cpp', 'Allocation was not overwritten.')),
        ('2.2', lambda s: snippet_test(s, '2.2', 't2.2.cpp', 'Allocated 8192 bytes.')),
        ('2.3', pci_test),
        ('2.4', lambda s: snippet_test(s, '2.4', 't2.4.cpp', 'File contents: Hello from a test file.')),
        ('2.5', lambda s: snippet_test(s, '2.5', 't2.5.cpp', 'File in directory contents: Hello from a test directory.')),
        ('2.6', events_test), ('2.7', disk_read_test), ('2.8', disk_write_test),
        *[('3.' + str(n), lambda s, n=n: network_test(s, n)) for n in range(1, 9)],
        *[('4.' + str(n), lambda s, n=n: tasks_test(s, n)) for n in range(1, 4)],
        ('4.4', lambda s: snippet_test(s, '4.4', 't4.4.cpp', 'Hello from userspace!')),
        ('4.5', privileged_test), ('4.6', sleep_test),
        ('4.7', lambda s: snippet_test(s, '4.7', 't4.7.cpp', 'Read from initrd/hello.txt: Hello from the cOS initrd!')),
        ('4.8', reclaim_test), ('4.9', userspace_memory_test), ('4.10', arguments_test), ('4.11', compiler_test), ('4.12', cxx_test), ('4.13', selfhost_test),
        ('5.1', shell_test), ('5.2', gui_test),
        *[('5.' + str(n), lambda s, n=n: script_test(s, n)) for n in range(3, 7)],
        ('5.7', restart_shell_test),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/check')
    parser.add_argument('--native-toolchain', type=Path)
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--port', type=int, default=45540)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--timeout-scale', type=float, default=1)
    parser.add_argument('--only', nargs='+', help='README test IDs to run')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    tests = cases()
    if args.list:
        print('\n'.join(name for name, _ in tests))
        return 0
    if args.only:
        unknown = set(args.only) - {name for name, _ in tests}
        if unknown:
            parser.error('Unknown tests: ' + ', '.join(sorted(unknown)))
        tests = [(name, test) for name, test in tests if name in args.only]
    require(1 <= args.port <= 65535 and args.jobs > 0 and args.timeout_scale > 0, 'Invalid port, jobs or timeout scale')
    suite = Suite(args)
    results = []
    for name, test in tests:
        if name in ('4.11', '4.12', '4.13') and not suite.native:
            print('SKIP ' + name + ' (configure NATIVE_TOOLCHAIN to include GCC)', flush=True)
            results.append({'id': name, 'status': 'SKIP'})
            continue
        start = time.monotonic()
        try:
            test(suite)
        except (AssertionError, OSError, subprocess.SubprocessError, ValueError, IndexError, StopIteration) as error:
            print('FAIL ' + name + ': ' + str(error), flush=True)
            results.append({'id': name, 'status': 'FAIL', 'error': str(error)})
        else:
            print('PASS ' + name, flush=True)
            results.append({'id': name, 'status': 'PASS'})
        results[-1]['seconds'] = round(time.monotonic() - start, 2)
    (suite.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    counts = Counter(result['status'] for result in results)
    print(f"{counts['PASS']} passed, {counts['FAIL']} failed, {counts['SKIP']} skipped. Evidence: {suite.output}")
    return 1 if counts['FAIL'] else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
