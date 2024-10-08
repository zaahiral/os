#!/usr/bin/env python3
"""Prepare the optional FAT32 disk for compiling the kernel inside cOS."""

import argparse
from pathlib import Path
import shlex
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--flags', required=True)
    parser.add_argument('sources', nargs='+')
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    staging = output.parent / 'selfhost-files'
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()
    for directory in ('src', 'include'):
        shutil.copytree(source / directory, staging / directory)
    shutil.copy2(source / 'targets/x86_64/linker.ld', staging / 'linker.ld')
    shutil.copy2(source / 'toolchain/selfbuild.c', staging / 'build.c')
    for directory in ('obj', 'rsp'):
        (staging / directory).mkdir()
    flags = shlex.split(args.flags)
    includes = ['-Idisk/include/kernel', '-Idisk/include/kernel/x86_64']
    commands = []
    objects = []
    for index, path in enumerate(args.sources):
        relative = Path(path).resolve().relative_to(source)
        guestPath = 'disk/' + relative.as_posix()
        obj = 'disk/obj/' + str(index) + '.o'
        objects.append(obj)
        if relative.suffix == '.cpp':
            response = 'rsp/' + str(index) + '.rsp'
            arguments = flags + includes + ['-c', guestPath, '-o', obj]
            (staging / response).write_text('\n'.join(arguments) + '\n')
            commands.append('g++ @disk/' + response)
        else:
            commands.append('nasm ' + ' '.join(includes + ['-f', 'elf64', guestPath, '-o', obj]))
    (staging / 'link.rsp').write_text('\n'.join(
        ['-n', '-T', 'disk/linker.ld', '-o', 'disk/kernel.bin'] + objects) + '\n')
    commands.append('ld @disk/link.rsp')
    (staging / 'build.txt').write_text('\n'.join(commands) + '\n')
    for path in staging.rglob('*'):
        if len('disk/' + path.relative_to(staging).as_posix()) > 179:
            raise ValueError('Path exceeds the cOS limit: ' + str(path))
    output.unlink(missing_ok=True)
    subprocess.run(['mformat', '-i', str(output), '-C', '-F', '-T', '131072', '-h', '64', '-s', '32', '::'], check=True)
    subprocess.run(['mcopy', '-s', '-i', str(output)] +
                   [str(path) for path in sorted(staging.iterdir())] + ['::'], check=True)
    print(str(len(objects)) + ' kernel sources on ' + str(output))


if __name__ == '__main__':
    main()
