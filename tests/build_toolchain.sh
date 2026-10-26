#!/bin/sh
set -eu
target=x86_64-elf
prefix=/opt/cross
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
curl --fail --location --retry 3 -o binutils.tar.bz2 https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.bz2
echo '3068128c75cda9f898ccb4211d360246e8e195ffcc9dfb655b23ae23a54800e8  binutils.tar.bz2' | sha256sum -c -
curl --fail --location --retry 3 -o gcc.tar.xz https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz
echo '438fd996826b0c82485a29da03a72d71d6e3541a83ec702df4271f6fe025d24e  gcc.tar.xz' | sha256sum -c -
tar -xf binutils.tar.bz2
tar -xf gcc.tar.xz
mkdir build-binutils build-gcc
cd build-binutils
../binutils-2.47/configure --target="$target" --prefix="$prefix" --enable-targets=x86_64-w64-mingw32 --with-sysroot --disable-nls --disable-werror
make -j2
make install
cd ../build-gcc
export PATH="$prefix/bin:$PATH"
../gcc-15.2.0/configure --target="$target" --prefix="$prefix" --disable-nls \
    --enable-languages=c,c++ --without-headers --disable-multilib
make -j2 all-gcc all-target-libgcc
make install-gcc install-target-libgcc
