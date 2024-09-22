#!/bin/bash
# Builds the cOS cross toolchain: binutils and GCC for the x86_64-cos target.
# Usage: build-cos-toolchain.sh <work directory> <install prefix> [jobs]
set -e

BINUTILS_VERSION=2.42
GCC_VERSION=14.2.0
TARGET=x86_64-cos

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORK=$(realpath -m "$1")
PREFIX=$(realpath -m "$2")
JOBS=${3:-$(nproc)}
# GCC expects the sysroot to have an include directory, even before the C library is installed into it.
mkdir -p "$WORK" "$PREFIX/sysroot/usr/include"
cd "$WORK"

# Download the sources, and apply the patches that add the cOS target.
for url in https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz \
           https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz; do
    [ -f "$(basename "$url")" ] || curl -sSLO "$url"
done
rm -rf binutils-$BINUTILS_VERSION gcc-$GCC_VERSION build-binutils build-gcc
tar xf binutils-$BINUTILS_VERSION.tar.xz
tar xf gcc-$GCC_VERSION.tar.xz
patch -d binutils-$BINUTILS_VERSION -p1 < "$SCRIPT_DIR/patches/binutils-$BINUTILS_VERSION-cos.patch"
patch -d gcc-$GCC_VERSION -p1 < "$SCRIPT_DIR/patches/gcc-$GCC_VERSION-cos.patch"

mkdir build-binutils
cd build-binutils
../binutils-$BINUTILS_VERSION/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot="$PREFIX/sysroot" --disable-nls --disable-werror
make -j"$JOBS"
make install
cd ..

# The C library is not built yet, so GCC is configured without its headers, and only the compilers and libgcc are built.
# --with-newlib tells libgcc that the C library will be newlib, so it does not look for its headers yet.
export PATH="$PREFIX/bin:$PATH"
mkdir build-gcc
cd build-gcc
../gcc-$GCC_VERSION/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot="$PREFIX/sysroot" --without-headers --with-newlib \
    --enable-languages=c,c++ --disable-nls --disable-multilib --disable-shared
make -j"$JOBS" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
