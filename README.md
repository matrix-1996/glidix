Glidix
======

Glidix is an experimental UNIX-clone OS, designed to run on x86_64. It is made mainly for experiments, and is designed to be easy to build and run on 64-bit x86-family processors. We are aiming to make it very customizable and therefore optimizeable for anyone's needs.

## Requirements

 * x86_64 (aka AMD64) CPU; SSE4.1 support is recommended.
 * 64MB RAM (2GB recommended for GUI).

## Hardware Support

 * Basic hardware: PCI, VGA text mode, PIT, APIC (including timer). This is implemented in the kernel itself; all the things below are implemented as separate modules.
 * Storage devices: IDE and AHCI; IDE is incomplete.
 * Graphics: BGA. Hardware acceleration for various cards is planned.
 * Input devices: PS/2 mouse and keyboard
 * Ethernet cards: NE2000 (promiscous only), Intel 8254x (*PRO/1000 MT Desktop*, and *T/MT Server*), and broken support for VirtIO net.

## Features

 * Capable of booting from an MBR using a custom bootloader; UEFI boot planned.
 * A simple graphical interface, currently under heavy development.
 * Network stack supporting IPv4 and IPv6, ICMP to some degree, UDP, DNS and TCP. Supports autoconfiguration with DHCP, ALIC (*Automatic Local IP Configuration*, for IPv4 link-local addresses when DHCP is unavailable), and SLAAC for IPv6.
 * Supports the ISO 9660 filesystem, plus a custom filesystem called Glidix File System (GXFS), which can store Glidix-specific metadata such as application permissions. Support for variants of FAT is planned.
 * Implements the POSIX API using a custom C library.
 * The GNU toolchain (binutils and GCC) can run natively on Glidix (the source code must be patched to support the `x86_64-glidix` target).

## GCC

The source comes with GCC and binutils patches, and allows a cross-compiler to be built, targetting Glidix. A native compiler can also be compiled from this. The Glidix target for GCC (`x86_64-glidix`) supports some additional command-line options:

 * `-mconsole` - link an executable as a console application (the default; really a no-op).
 * `-mgui` - link an executable as a GUI application. This is done by linking against `crtgui.o`, which adds the appropriate annotation to the `.glidix.annot` section. By default, it also links against `libgwm` and `libddi`; to prevent this, use `-mnoguilibs`.
 * `-module` - link as a Glidix kernel module. This avoids linking with `libc` and other system libraries, and outputs an object file (typically with `.gkm` extension), which can be loaded by the Glidix kernel. Note that source files must be compiled with `-mcmodel=large`.
 * `-mnoguilibs` - when used with `-mgui`, do not link against GUI libraries.

## Planned features

 * FAT filesystem support.
 * USB support.
 * Booting from UEFI, and supporting GPT.
 * Hardware-accelerated graphics.
 * More drivers and porting more applications.
 * Audio support.
