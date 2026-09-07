# libusb-1.0 for Windows (MinGW, x86-64)

`lib/libusb-1.0.a`, cross-built from the source tarball beside it:

    tar xf libusb-1.0.29.tar.bz2 && cd libusb-1.0.29
    ./configure --host=x86_64-w64-mingw32 --enable-static --disable-shared --disable-udev
    make

    libusb-1.0.29.tar.bz2
    sha256 5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85

The include path given to the compiler is this directory's `include`, the PARENT
of `libusb-1.0/`, because the sources include <libusb-1.0/libusb.h>.

## Why the source is here

libusb is LGPL-2.1-or-later and this is linked STATICALLY, so it becomes part of
jscope.exe. Section 6 then asks that whoever receives that executable be able to
relink it against their own build of libusb.

Publishing the application's own source satisfies that -- 6(a) accepts the
complete source of the work that uses the library -- but only if the library
source that was ACTUALLY LINKED is available too. So the tarball is here rather
than merely cited, and the archive beside it was built from it by the command
above rather than obtained from somewhere unrecorded. A version number in a
comment would not let anyone reproduce the binary; this does.
