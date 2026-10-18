/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Android audio backend: forwards each 48kHz/SOUNDBUFLEN-sample
 *          batch sound_poll() produces straight to android_bridge.h's
 *          audio sink (msdos_host.cpp's ring buffer, which
 *          Java/AudioOutput.kt drains via msdoshost_fill_audio). Requires
 *          sound_is_float == 0 (set by msdos_host.cpp before do_start()) so
 *          givealbuffer() always receives ready-to-use int16 samples instead
 *          of float.
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdint.h>

#include <86box/86box.h>
#include <86box/android_bridge.h>
#include <86box/sound.h>

void android_ui_deliver_audio(const int16_t *samples, int nframes);

void
inital(void)
{
    /* Nothing to open: android_bridge's sink is registered directly by
     * msdos_host.cpp before the session starts. */
}

void
closeal(void)
{
}

void
givealbuffer(const void *buf)
{
    android_ui_deliver_audio((const int16_t *) buf, SOUNDBUFLEN);
}

void
givealbuffer_music(const void *buf)
{
    (void) buf; /* MUNT/FluidSynth/RTMIDI are all disabled on Android. */
}

void
givealbuffer_wt(const void *buf)
{
    (void) buf;
}

void
givealbuffer_cd(const void *buf)
{
    (void) buf; /* No CD-DA audio path bundled yet. */
}

void
givealbuffer_fdd(const void *buf, const uint32_t size)
{
    (void) buf;
    (void) size;
}

void
givealbuffer_hdd(const void *buf, const uint32_t size)
{
    (void) buf;
    (void) size;
}
