# wdi-simple for Windows (MinGW, x86-64)

`wdi-simple.exe` binds the WinUSB driver to the Hantek 1008C (`0783:5725`) —
the step that otherwise means downloading Zadig and doing it by hand. The
Windows installer (`packaging/jscope.iss`) runs it, elevated, when the
"Install the USB driver" task is ticked.

It is libwdi's own command-line example, cross-built from the source tarball
beside it, with Microsoft's WinUSB co-installers embedded:

    tar xf libwdi-1.5.1.tar.bz2 && cd libwdi-1.5.1
    ./autogen.sh
    ./configure --host=x86_64-w64-mingw32 --disable-32bit --enable-64bit \
                --with-wdkdir="<extracted>/Program Files/Windows Kits/8.0" \
                --enable-examples-build --disable-debug --disable-shared
    # autoconf cannot look for files when cross-compiling, so the two layout
    # values it would have found are set by hand -- to the layout of the
    # Microsoft package below:
    sed -i 's|/\* #undef COINSTALLER_DIR \*/|#define COINSTALLER_DIR "wdf"|; \
            s|/\* #undef X64_DIR \*/|#define X64_DIR "x64"|' config.h
    make
    x86_64-w64-mingw32-strip examples/wdi-simple.exe

    libwdi-1.5.1.tar.bz2   git archive of tag v1.5.1, github.com/pbatard/libwdi
    sha256 811903ef1a195cb1203db92281a19a7eb299ba9d63befc9c6568a9320fc75c1f

The WDK is Microsoft's "WDK 8 redistributable components", extracted with
`msiextract`:

    https://go.microsoft.com/fwlink/p/?LinkID=253170   (wdfcoinstaller.msi)
    sha256 29314207814ce9d5d73695f7e9239539cf37c79e750b9d5ea5a5ef5487a583d6

    wdi-simple.exe
    sha256 91903d657a4504c2915cc8c2de7fc02cdb8b503289415bd5b92860866d976386

## Licences

libwdi and wdi-simple are LGPL-3.0-or-later (`COPYING-LGPL.txt`); the source
they were built from is the tarball here. The embedded `WdfCoInstaller01011.dll`
and `winusbcoinstaller2.dll` are Microsoft redistributable components, shipped
as the WDK redistribution terms allow — inside the driver package they belong
to, unmodified.

## What it does to a machine

Binding WinUSB REPLACES Hantek's own driver for this device: Hantek's software
stops seeing the scope until the Hantek driver is put back (Device Manager ->
the device -> Update driver). The installer's task says so in its own label, and
it never runs during a silent install -- which is what an update is.

libwdi signs the driver package it generates with a certificate it creates on
the spot and adds to the machine's trusted publishers — the same thing Zadig
does, because Windows will not install an unsigned package.
