#!/bin/bash
# Builds the assembler, the linker and the C compiler as cOS programs, so that cOS can compile C programs itself.
# This is a Canadian cross: the programs are built on Linux, run on cOS, and make programs for cOS.
# Run build-cos-toolchain.sh and build-cos-newlib.sh first, with the same work directory and install prefix.
# The programs, headers and libraries are installed into <output directory>/initrd, which is added to the initrd with
# cmake -DNATIVE_TOOLCHAIN=<output directory>. GCC's prefix is /initrd/usr, so it finds cc1, the headers and the
# libraries where they are in the initrd.
# Usage: build-cos-native.sh <work directory> <install prefix> <output directory> [jobs]
set -e

BINUTILS_VERSION=2.42
GCC_VERSION=14.2.0
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
    --with-native-system-header-dir=/initrd/usr/include --with-newlib --enable-languages=c --disable-multilib --disable-lto \
    --disable-plugin --without-isl --disable-threads --disable-gettext --disable-gcov
make -j"$JOBS" all-gcc
cd ..

# The shell runs programs from initrd/bin, so GCC's bindir is initrd/bin, which also lets the driver find cc1 from
# where it is. Only the driver is kept there, with the assembler, the linker and ar.
rm -rf "$OUTPUT/initrd"
make -C build-native-gcc install-gcc DESTDIR="$OUTPUT"
cd "$OUTPUT/initrd"
find bin ! -type d ! -name gcc -delete
rm -rf usr/share usr/libexec/gcc/$TARGET/$GCC_VERSION/install-tools usr/lib/gcc/$TARGET/$GCC_VERSION/install-tools
$TARGET-strip bin/gcc $(find usr/libexec -type f)
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
