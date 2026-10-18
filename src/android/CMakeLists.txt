#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for the Android platform layer.
#
# Authors: FujiNet Project
#
#          Copyright 2026 FujiNet Project.
#

add_library(plat OBJECT
    android_plat.c
    android_stubs.c
    # Reused verbatim from src/unix/ -- all three are pure POSIX/bionic-
    # compatible with zero SDL/Qt dependency (see the exploration notes in
    # msdos_host.h and tools/86box/build-86box-core.sh):
    ../unix/unix_dynld.c            # dynld_module()/dynld_close() (net_pcap.c, prt_ps.c, mdsx_dll.c)
    ../unix/unix_netsocket.c        # plat_netsocket_*() (net_modem.c)
    ../unix/dummy_floppy_ioctl.c    # fdd_*_host_device()/floppy_ioctl_*_buffering() (no raw floppy access on Android)
    ../unix/sdl_cdrom.c             # cassette/cartridge/floppy/cdrom/mo/rdisk/tape mount+eject (despite the filename, no SDL dependency)
    ../unix/dummy_cdrom_ioctl.c     # ioctl_open() (no raw host CD-ROM access on Android)
)

add_library(ui OBJECT android_ui.c android_audio.c)
target_link_libraries(ui PRIVATE log)
