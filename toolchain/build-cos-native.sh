#!/bin/bash
# Builds the assembler, the linker and the C and C++ compilers as cOS programs, so that cOS can compile its own sources.
# This is a Canadian cross: the programs are built on Linux, run on cOS, and make programs for cOS.
# Run build-cos-toolchain.sh and build-cos-newlib.sh first, with the same work directory and install prefix.
# The programs, headers and libraries are installed into <output directory>/initrd, which is added to the initrd with
# cmake -DNATIVE_TOOLCHAIN=<output directory>. GCC's prefix is /initrd/usr, so it finds cc1, the headers and the
# libraries where they are in the initrd.
# Usage: build-cos-native.sh <work directory> <install prefix> <output directory> [jobs]
set -e

BINUTILS_VERSION=2.42
GCC_VERSION=14.2.0
NASM_VERSION=3.02
TARGET=x86_64-cos

WORK=$(realpath -m "$1")
PREFIX=$(realpath -m "$2")
OUTPUT=$(realpath -m "$3")
JOBS=${4:-$(nproc)}
SYSROOT="$PREFIX/sysroot"
export PATH="$PREFIX/bin:$PATH"
cd "$WORK"

# The options that leave out what cOS cannot run yet, such as plugins, threads and translations.
COMMON_OPTIONS="--build=$(gcc -dumpmachine) --host=$TARGET --target=$TARGET --prefix=/initrd/usr --disable-nls --disable-shared \
    --disable-plugins --without-zstd"

rm -rf build-native-binutils build-native-gcc
mkdir build-native-binutils
cd build-native-binutils
../binutils-$BINUTILS_VERSION/configure $COMMON_OPTIONS --disable-werror --disable-gdb --disable-gdbserver --disable-sim \
    --disable-gprofng --disable-libctf --without-debuginfod
make -j"$JOBS" all-gas all-ld all-binutils
cd ..

# GCC needs GMP, MPFR and MPC, which are built for cOS along with it from the GCC source tree.
# Their own copies of config.sub need to know the cOS target too. This is done once per source tree.
# GCC's own copy of gettext is only needed for translations, so it is left out.
if [ ! -e gcc-$GCC_VERSION/gmp ]; then
    (cd gcc-$GCC_VERSION && ./contrib/download_prerequisites --no-graphite)
    for file in gmp/configfsf.sub mpfr/config.sub mpc/build-aux/config.sub; do
        sed -i -e 's/| haiku\* |/| haiku* | cos* |/' -e 's/| -haiku\* |/| -haiku* | -cos* |/' "gcc-$GCC_VERSION/$file"
    done
fi
mkdir build-native-gcc
cd build-native-gcc
../gcc-$GCC_VERSION/configure $COMMON_OPTIONS --bindir=/initrd/bin --with-sysroot=/ --with-build-sysroot="$SYSROOT" \
    --with-native-system-header-dir=/initrd/usr/include --with-gxx-include-dir=/initrd/usr/include/c++/$GCC_VERSION \
    --with-newlib --enable-languages=c,c++ --disable-multilib --disable-lto \
    --disable-plugin --without-isl --disable-threads --disable-gettext --disable-gcov
make -j"$JOBS" all-gcc
cd ..

# The shell runs programs from initrd/bin, so GCC's bindir is initrd/bin, which also lets the driver find cc1 from
# where it is. Only the drivers are kept there, with the assembler, the linker and ar.
rm -rf "$OUTPUT/initrd"
make -C build-native-gcc install-gcc DESTDIR="$OUTPUT"
cd "$OUTPUT/initrd"
find bin ! -type d ! -name gcc ! -name g++ -delete
rm -rf usr/share usr/libexec/gcc/$TARGET/$GCC_VERSION/install-tools usr/lib/gcc/$TARGET/$GCC_VERSION/install-tools
$TARGET-strip bin/gcc bin/g++ $(find usr/libexec -type f)
cd "$WORK"
$TARGET-strip -o "$OUTPUT/initrd/bin/as" build-native-binutils/gas/as-new
$TARGET-strip -o "$OUTPUT/initrd/bin/ld" build-native-binutils/ld/ld-new
$TARGET-strip -o "$OUTPUT/initrd/bin/ar" build-native-binutils/binutils/ar

# Every program needs the C library and libgcc. The libraries are built with debugging information, which ld does not need.
mkdir -p "$OUTPUT/initrd/usr/include"
cp -r "$SYSROOT/usr/include/." "$OUTPUT/initrd/usr/include/"
cp "$SYSROOT/usr/lib/"{crt0.o,crti.o,crtn.o,libcos.a} "$OUTPUT/initrd/usr/lib/"
cp "$PREFIX/lib/gcc/$TARGET/$GCC_VERSION/"{crtbegin.o,crtend.o,libgcc.a} "$OUTPUT/initrd/usr/lib/gcc/$TARGET/$GCC_VERSION/"
for library in libc.a libm.a; do
    $TARGET-strip --strip-debug -o "$OUTPUT/initrd/usr/lib/$library" "$SYSROOT/usr/lib/$library"
done

# A freestanding libstdc++ installs its own header subset and the language support library, without hosted I/O.
# Use the cross compiler here, because the compiler just built runs on cOS.
rm -rf build-native-libstdcxx
mkdir build-native-libstdcxx
cd build-native-libstdcxx
../gcc-$GCC_VERSION/libstdc++-v3/configure --build=$(gcc -dumpmachine) --host=$TARGET --target=$TARGET \
    --prefix=/initrd/usr --with-gxx-include-dir=/initrd/usr/include/c++/$GCC_VERSION \
    --with-newlib --disable-hosted-libstdcxx --disable-shared --disable-multilib --disable-nls \
    --disable-threads --disable-libstdcxx-pch --disable-libstdcxx-verbose
make -j"$JOBS"
make install DESTDIR="$OUTPUT"
rm -f "$OUTPUT/initrd/usr/lib/"{libstdc++.a,libstdc++.la,libsupc++.la}
$TARGET-strip --strip-debug "$OUTPUT/initrd/usr/lib/libsupc++.a"
rm -rf "$OUTPUT/initrd/usr/share"
cd "$WORK"

# NASM supplies the Intel-syntax assembler used by the kernel's .asm files.
[ -f nasm-$NASM_VERSION.tar.xz ] || curl -fLsSLO https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VERSION/nasm-$NASM_VERSION.tar.xz
rm -rf nasm-$NASM_VERSION
tar xf nasm-$NASM_VERSION.tar.xz
cd nasm-$NASM_VERSION
sed -i 's/| haiku\* |/| haiku* | cos* |/' autoconf/helpers/config.sub
# cOS uses Unix path separators, which NASM does not recognise from __cos__ yet.
./configure --build=$(gcc -dumpmachine) --host=$TARGET --prefix=/initrd --disable-gdb CPPFLAGS=-DPATHSTYLE=1
make -j"$JOBS" nasm
$TARGET-strip -o "$OUTPUT/initrd/bin/nasm" nasm
