#! /bin/sh

rm -rf mipdir
mkdir mipdir || exit 1
mipdir="`realpath mipdir`"
mkdir -p $1

export DESTDIR="$mipdir"

# libc
cd libc
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/libc.mip || exit 1

# shutils
rm -rf mipdir
mkdir mipdir || exit 1
cd shutils
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/shutils.mip --setup=/usr/libexec/shutils-setup.sh || exit 1

# modules
rm -rf mipdir
mkdir mipdir || exit 1
cd modules
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/modules.mip

# sh
rm -rf mipdir
mkdir mipdir || exit 1
cd sh
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/sh.mip

# binutils
if [ "$MIP_BINUTILS" = "yes" ]
then
	rm -rf mipdir
	mkdir mipdir || exit 1
	cd binutils
	make install || exit 1
	cd ..
	build-tools/mkmip mipdir $1/binutils.mip
fi

# libz
rm -rf mipdir || exit 1
cd libz
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/libz.mip

# dynld
rm -rf mipdir || exit 1
cd dynld
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/dynld.mip

# libpng
rm -rf mipdir || exit 1
cd libpng
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/libpng.mip

# freetype
rm -rf mipdir || exit 1
cd freetype
make install || exit 1
cd ..
build-tools/mkmip mipdir $1/freetype.mip
