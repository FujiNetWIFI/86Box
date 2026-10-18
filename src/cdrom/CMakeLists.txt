#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024-2026 Jasmine Iwanek.
#

set(WITH_SYSTEM_ZLIB ON)
set(WITH_SYSTEM_ZSTD ON)
add_subdirectory(libchdr)

add_library(cdrom OBJECT
    cdrom.c
    cdrom_aaru.c
    cdrom_image.c
    cdrom_image_viso.c
    cdrom_mitsumi.c
    cdrom_mke.c
    cdrom_hitachi.c
    cdrom_philips.c
    cdrom_sony.c
    cdrom_cm153.c
    cm100.c
    cm100_protocol.c
    cm100_datapath.c
    cm153_uart.c
    cdrom_chd.c
)
target_link_libraries(cdrom chdr-static)

if(FUJINET_ANDROID)
    # Cross-compiled libsndfile (tools/86box/build-86box-core.sh), pinned via
    # SNDFILE_LIBRARY/_INCLUDE_DIR cache vars set in app/src/main/cpp/
    # CMakeLists.txt -- a plain pkg-config lookup here would otherwise
    # silently resolve to the build host's own sndfile.pc (wrong ISA).
    target_include_directories(cdrom PRIVATE "${SNDFILE_INCLUDE_DIR}")
    target_link_libraries(86Box "${SNDFILE_LIBRARY}")
else()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
    if(NOT WIN32)
        target_include_directories(86Box PRIVATE PkgConfig::SNDFILE)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        target_include_directories(cdrom PRIVATE /usr/local/include)
    endif()
    target_link_libraries(86Box PkgConfig::SNDFILE)
endif()

if (WIN32 AND NOT FUJINET_ANDROID)
    # MSYS2
    target_link_libraries(86Box -static ${SNDFILE_STATIC_LIBRARIES})
endif()
