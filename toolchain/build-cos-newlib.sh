#!/bin/bash
# Builds newlib for cOS, and libcos, which connects newlib to the cOS system calls, into the toolchain's sysroot.
# Run build-cos-toolchain.sh first, with the same install prefix.
# Usage: build-cos-newlib.sh <work directory> <install prefix> [jobs]
set -e

NEWLIB_VERSION=4.4.0.20231231
TARGET=x86_64-cos

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(dirname "$SCRIPT_DIR")
WORK=$(realpath -m "$1")
PREFIX=$(realpath -m "$2")
JOBS=${3:-$(nproc)}
SYSROOT="$PREFIX/sysroot"
export PATH="$PREFIX/bin:$PATH"
mkdir -p "$WORK"
cd "$WORK"

[ -f newlib-$NEWLIB_VERSION.tar.gz ] || curl -sSLO https://sourceware.org/pub/newlib/newlib-$NEWLIB_VERSION.tar.gz
rm -rf newlib-$NEWLIB_VERSION build-newlib build-libcos
tar xzf newlib-$NEWLIB_VERSION.tar.gz
patch -d newlib-$NEWLIB_VERSION -p1 < "$SCRIPT_DIR/patches/newlib-$NEWLIB_VERSION-cos.patch"

# newlib does not know cOS, so it leaves the operating system functions, such as read and sbrk, to libcos.
mkdir build-newlib
cd build-newlib
../newlib-$NEWLIB_VERSION/configure --target=$TARGET --prefix=/usr --disable-multilib \
    --enable-newlib-io-long-long --enable-newlib-io-c99-formats CFLAGS_FOR_TARGET="-O2 -g"
make -j"$JOBS"
make DESTDIR="$SYSROOT" install
cd ..

# newlib installs into a directory named after the target, but GCC looks in usr/include and usr/lib of the sysroot.
mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib"
cp -r "$SYSROOT/usr/$TARGET/include/." "$SYSROOT/usr/include/"
cp -r "$SYSROOT/usr/$TARGET/lib/." "$SYSROOT/usr/lib/"
rm -rf "$SYSROOT/usr/$TARGET"

# libcos provides crt0.o, crti.o and crtn.o, which GCC links into every program, and the system call functions.
mkdir build-libcos
cd build-libcos
for source in crt0.c syscalls.c crti.S crtn.S; do
    $TARGET-gcc -O2 -g -iquote "$REPO_DIR/include/kernel" -c "$REPO_DIR/userspace/libcos/$source" -o "${source%.*}.o"
done
cp crt0.o crti.o crtn.o "$SYSROOT/usr/lib/"
rm -f "$SYSROOT/usr/lib/libcos.a"
$TARGET-ar rcs "$SYSROOT/usr/lib/libcos.a" syscalls.o
