/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Small platform globals/no-ops that don't warrant their own file:
 *          the joystick backend (src/unix/qt/sdl_joystick.c on desktop,
 *          which pulls in SDL2 -- Android has no real joystick hardware to
 *          enumerate here; game controllers are routed through the app's
 *          own touch/gamepad UI -> msdoshost_set_joystick_* instead) and the
 *          GL3 shader pipeline's config storage (config.c reads/writes
 *          gl3_shader_file unconditionally; the Android build never enables
 *          the shader pipeline that consumes it).
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdint.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/qt-glsl.h>

plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];

void
joystick_init(void)
{
    /* No real joysticks enumerated on Android. */
}

void
joystick_close(void)
{
}

void
joystick_process(uint8_t gp)
{
    (void) gp;
}

char gl3_shader_file[MAX_USER_SHADERS][512];

/* Fixed-size-window and slirp NIC globals: read/written by config.c and
 * network.c respectively regardless of frontend; Android has no fixed-size
 * window UI and no slirp NIC (src/network/CMakeLists.txt drops net_slirp.c). */
int fixed_size_x = 640;
int fixed_size_y = 480;
int slirp_card_num = 0;
