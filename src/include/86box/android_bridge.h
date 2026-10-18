/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Small C bridge between the Android platform layer
 *          (src/android/) and the app's native host (msdos_host.cpp, in the
 *          fujinet-go-msdos repo). Keeps the two repos' knowledge of each
 *          other to this one header: android_ui.c/android_audio.c own the
 *          real video_setblit()/sound_add_handler() registration and just
 *          forward decoded frames/audio through these sinks; msdos_host.cpp
 *          registers a sink and otherwise doesn't need to know how 86Box's
 *          video/audio subsystems work internally.
 */
#ifndef EMU_ANDROID_BRIDGE_H
#define EMU_ANDROID_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One decoded video frame, tightly packed XRGB8888 (pitch == width*4).
 * Invoked on whichever thread 86Box's video subsystem drives blits from
 * (a dedicated per-monitor blit thread; see src/video/video.c). */
typedef void (*android_frame_cb_t)(const uint32_t *xrgb8888, int width, int height, void *user);
void android_video_set_sink(android_frame_cb_t cb, void *user);

/* Current video geometry (last frame's width/height), 0x0 before the first
 * frame. Safe to poll from any thread. */
void android_video_get_geometry(int *width, int *height);

/* Interleaved stereo signed-16 audio, invoked once per sound_poll() batch
 * (48000Hz per src/sound/sound.c's SOUNDBUFLEN) on the emulator thread. */
typedef void (*android_audio_cb_t)(const int16_t *samples, int nframes, void *user);
void android_audio_set_sink(android_audio_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /*EMU_ANDROID_BRIDGE_H*/
