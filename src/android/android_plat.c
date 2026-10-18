/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Android platform support functions (plat.h).
 *
 *          Most of this is a straight port of src/unix/sdl_plat.c and
 *          src/unix/sdl_plat_unix.c with the SDL calls swapped for their
 *          POSIX/bionic equivalents -- 86Box's own file I/O, path handling,
 *          and misc helpers have no SDL dependency to begin with. The pieces
 *          that *do* need real platform work (video, audio, threading) live
 *          in android_ui.c / android_audio.c / this file's thread section.
 *
 *          The app always launches with `-P <box86Root>` (see msdos_host.cpp),
 *          which makes 86box.c itself register `<box86Root>/roms` and
 *          `<box86Root>/assets` as ROM/asset search paths and place the
 *          config file under `<box86Root>` -- so plat_init_rom_paths() /
 *          plat_init_asset_paths() (which add *additional*, XDG-style search
 *          locations on desktop) are no-ops here; there is no second
 *          "system-wide" location on Android.
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#define __USE_GNU 1
#include <pthread.h>

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/hdd.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/rom.h>
#include <86box/ui.h>
#include <86box/version.h>

#include "cpu.h"

/* Globals plat.h expects some platform to define (see src/unix/sdl_main.c /
 * sdl_render.c) that have no Qt/SDL-menu meaning on Android. */
int rctrl_is_lalt    = 0;
int update_icons     = 0;
int kbd_req_capture  = 0;
int hide_status_bar  = 1;
int hide_tool_bar    = 1;
int mouse_capture    = 0;
int infocus          = 1; /* the app is always "focused" while the session runs */

/*
 * File system manipulation
 */

int
plat_getcwd(char *bufp, int max)
{
    return getcwd(bufp, max) != 0;
}

int
plat_chdir(char *str)
{
    return chdir(str);
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int
plat_dir_check(char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return S_ISDIR(stats.st_mode);
}

int
plat_file_check(const char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return !S_ISDIR(stats.st_mode);
}

int
plat_dir_create(char *path)
{
    return mkdir(path, S_IRWXU);
}

void
plat_remove(char *path)
{
    remove(path);
}

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    struct tm     *calendertime;
    struct timeval t;
    time_t         curtime;
    size_t         used = 0;

    if (prefix != NULL)
        used = snprintf(bufp, 1024, "%s-", prefix);
    else
        bufp[0] = '\0';

    gettimeofday(&t, NULL);
    curtime      = time(NULL);
    calendertime = localtime(&curtime);

    if (used < 1024) {
        snprintf(bufp + used, 1024 - used,
                 "%d%02d%02d-%02d%02d%02d-%03d%s",
                 calendertime->tm_year, calendertime->tm_mon, calendertime->tm_mday,
                 calendertime->tm_hour, calendertime->tm_min, calendertime->tm_sec,
                 (int) (t.tv_usec / 1000), suffix);
    }
}

int
plat_is_block_device(const char *path)
{
    (void) path;
    return 0; /* the app never touches raw block devices */
}

int64_t
plat_get_block_device_size(const char *path)
{
    (void) path;
    return -1;
}

int
path_abs(char *path)
{
    return path[0] == '/';
}

void
path_normalize(char *path)
{
    (void) path;
}

/*
 * Common locations. The app always launches with `-P <box86Root>`, which
 * 86box.c uses as usr_path (roms/, assets/, and the config file all resolve
 * under it) -- so these just mirror usr_path rather than doing XDG lookups.
 */

void
plat_get_global_data_dir(char *outbuf, const size_t len)
{
    if (len == 0)
        return;
    strncpy(outbuf, usr_path, len);
    outbuf[len - 1] = '\0';
}

void
plat_get_global_config_dir(char *outbuf, const size_t len)
{
    plat_get_global_data_dir(outbuf, len);
}

void
plat_get_temp_dir(char *outbuf, uint8_t len)
{
    if (len == 0)
        return;
    strncpy(outbuf, usr_path, len);
    outbuf[len - 1] = '\0';
    path_slash(outbuf);
}

void
plat_get_vmm_dir(char *outbuf, const size_t len)
{
    /* No VM manager on this build. */
    if (len > 0)
        outbuf[0] = '\0';
}

void
plat_get_exe_name(char *s, int size)
{
    /* usr_path already anchors roms/assets/config; nothing meaningful to
     * add here (there is no "install directory" on Android). */
    if (size > 0)
        s[0] = '\0';
}

void
plat_init_rom_paths(void)
{
    /* usr_path/roms is already registered by 86box.c before this runs. */
}

void
plat_init_asset_paths(void)
{
    /* usr_path/assets is already registered by 86box.c before this runs. */
}

/*
 * String localization -- ported verbatim from sdl_plat.c.
 */

char *
plat_get_string(int i)
{
    switch (i) {
        case STRING_MOUSE_CAPTURE:
            return "Click to capture mouse";
        case STRING_MOUSE_RELEASE:
            return "Press CTRL-END to release mouse";
        case STRING_MOUSE_RELEASE_MMB:
            return "Press CTRL-END or middle button to release mouse";
        case STRING_PCAP_ERROR_NO_DEVICES:
            return "No PCap devices found.";
        case STRING_PCAP_ERROR_INVALID_DEVICE:
            return "Invalid PCap device.";
        case STRING_GHOSTSCRIPT_ERROR:
            return "Unable to initialize Ghostscript.";
        case STRING_GHOSTPCL_ERROR:
            return "Unable to initialize GhostPCL.";
        case STRING_HW_NOT_AVAILABLE_MACHINE:
            return "Machine \"%s\" is not available due to missing ROMs in the roms/machines directory. Switching to an available machine.";
        case STRING_HW_NOT_AVAILABLE_VIDEO:
            return "Video card \"%s\" is not available due to missing ROMs in the roms/video directory. Switching to an available video card.";
        case STRING_HW_NOT_AVAILABLE_DEVICE:
            return "Device \"%s\" is not available due to missing ROMs. Ignoring the device.";
        case STRING_HW_NOT_AVAILABLE_TITLE:
            return "Hardware not available";
        case STRING_NET_ERROR:
            return "Failed to initialize network driver:\n\n%s\n\nThe network configuration will be switched to the null driver.";
        case STRING_ESCP_ERROR:
            return "Unable to find Dot-Matrix fonts.";
        case STRING_EDID_TOO_LARGE:
            return "EDID file \"%s\" is too large.";
        case STRING_CDROM_OPEN_ISO_ERROR:
            return "Unable to open image or folder \"%s\".";
        case STRING_CDROM_OPEN_CUE_ERROR:
            return "Unable to open cue sheet \"%s\".";
        case STRING_CDROM_OPEN_MDS_ERROR:
            return "Unable to open MDS file \"%s\".";
        case STRING_CDROM_LOAD_IMAGE_ERROR:
            return "Unable to load CD-ROM image \"%s\".";
        case STRING_CDROM_LOAD_MDSX_ERROR:
            return "Unable to load image \"%s\".";
        case STRING_CDROM_DVD_IN_CD_DRIVE:
            return "The DVD image \"%s\" has been inserted into a drive that does not support DVD media and will be ignored.";
        case STRING_CHARDEV_CONNECT_ERROR:
            return "%s: Could not connect to %s: %s";
        case STRING_CHARDEV_CREATE_ERROR:
            return "%s: Could not create %s: %s";
        case STRING_CHARDEV_ATTACHED:
            return "%s: Attached to %s";
        case STRING_CHARDEV_VCON_IN_USE:
            return "%s: Virtual console already in use by %s";
        case STRING_CHARDEV_TERMINAL_ERROR:
            return "%s: Could not create terminal: %s";
    }
    return "";
}

int
plat_language_code(char *langcode)
{
    (void) langcode;
    return 0;
}

void
plat_language_code_r(int id, char *outbuf, int len)
{
    (void) id;
    if (len > 0)
        outbuf[0] = '\0';
}

/*
 * Path manipulation -- ported verbatim from sdl_plat.c.
 */

void
path_slash(char *path)
{
    if (path[strlen(path) - 1] != '/')
        strcat(path, "/");
    path_normalize(path);
}

const char *
path_get_slash(char *path)
{
    return (path[strlen(path) - 1] != '/') ? "/" : "";
}

char *
path_get_basename(const char *path)
{
    int c = (int) strlen(path);
    while (c > 0) {
        if (path[c] == '/')
            return ((char *) &path[c + 1]);
        c--;
    }
    return ((char *) path);
}

char *
path_get_filename(char *s)
{
    int c = strlen(s) - 1;
    while (c > 0) {
        if (s[c] == '/' || s[c] == '\\')
            return (&s[c + 1]);
        c--;
    }
    return s;
}

char *
path_get_extension(char *s)
{
    int c = strlen(s) - 1;
    if (c <= 0)
        return s;
    while (c && s[c] != '.')
        c--;
    if (!c)
        return (&s[strlen(s)]);
    return (&s[c + 1]);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    path_slash(dest);
    strcat(dest, s2);
}

void
path_get_dirname(char *dest, const char *path)
{
    int   c   = (int) strlen(path);
    char *ptr = (char *) path;

    while (c > 0) {
        if (path[c] == '/' || path[c] == '\\') {
            ptr = (char *) &path[c];
            break;
        }
        c--;
    }
    while (path < ptr)
        *dest++ = *path++;
    *dest = '\0';
}

/*
 * Memory management -- ported verbatim from sdl_plat_unix.c.
 */

void *
plat_mmap(size_t size, uint8_t executable)
{
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
                      MAP_ANON | MAP_PRIVATE, -1, 0);
    return (ret == MAP_FAILED) ? NULL : ret;
}

void
plat_munmap(void *ptr, size_t size)
{
    munmap(ptr, size);
}

/*
 * Timers -- POSIX monotonic clock in place of SDL_GetPerformanceCounter.
 */

uint64_t
plat_timer_read(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

uint32_t
plat_get_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ((uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL);
}

void
plat_delay_ms(uint32_t count)
{
    struct timespec ts;
    ts.tv_sec  = count / 1000;
    ts.tv_nsec = (long) (count % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/*
 * Emulator support
 */

void
plat_power_off(void)
{
    confirm_exit_cmdl = 0;
    hdd_image_sync_all();
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated. */
    cycles -= 99999999;

    extern volatile int cpu_thread_run;
    cpu_thread_run = 0;
}

void
plat_pause(int p)
{
    if ((!!p) == dopause)
        return;
    if ((p == 0) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();
    do_pause(p);
}

void
plat_mouse_capture(int on)
{
    mouse_capture = on;
}

int
plat_vidapi(const char *api)
{
    (void) api;
    return 0;
}

char *
plat_vidapi_name(int i)
{
    (void) i;
    return "default";
}

void
plat_get_cpu_string(char *outbuf, uint8_t len)
{
    strncpy(outbuf, "Unknown", len);
}

int
plat_run_command(const char *cmd, const char **env, const char *title)
{
    (void) cmd;
    (void) env;
    (void) title;
    return 1; /* no external terminal/command execution on Android */
}

int
stricmp(const char *s1, const char *s2)
{
    return strcasecmp(s1, s2);
}

int
strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}

/*
 * Threads -- pthread_setname_np is available on bionic.
 */

void
plat_set_thread_name(void *thread, const char *name)
{
    char truncated[16];
    strncpy(truncated, name, sizeof(truncated) - 1);
    truncated[sizeof(truncated) - 1] = '\0';
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated);
}

/*
 * Dynamic library loading (dynld_module/dynld_close) is provided by
 * ../unix/unix_dynld.c, reused directly in src/android/CMakeLists.txt.
 */
