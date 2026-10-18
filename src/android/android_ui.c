/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Android UI stubs + video sink + start/stop lifecycle.
 *
 *          There is no window, status bar, or menu to update on Android, so
 *          most of ui.h is a no-op here (mirroring src/unix/sdl_ui.c's own
 *          no-ops for the panes/text/tip functions it doesn't wire to a
 *          real status bar). The one real piece of platform work is the
 *          video sink: video_setblit() registers a callback 86Box calls
 *          from a dedicated per-monitor blit thread (src/video/video.c);
 *          android_blit_shim() copies each row out of the monitor's bitmap
 *          (mirroring src/unix/sdl_render.c::sdl_blit_shim's row-copy) and
 *          forwards it through android_bridge.h to msdos_host.cpp, which
 *          feeds it to the app's existing session_runtime.cpp frame path.
 *
 *          do_start()/do_stop() replace src/unix/sdl_main.c's SDL-threaded
 *          main_thread with 86Box's own cross-platform thread_t (thread.cpp,
 *          std::thread-backed under CPPTHREADS -- already the non-Windows
 *          default, no SDL needed).
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include <86box/86box.h>
#include <86box/android_bridge.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/video.h>

#include <android/log.h>

#define LOG_TAG "86BoxUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

/* --- message boxes: log only, there is no dialog host on Android --------- */

int
ui_msgbox_header(int flags, char *header, char *message)
{
    LOGW("msgbox[%d] %s: %s", flags, header ? header : EMU_NAME, message ? message : "");
    return 0;
}

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

/* --- status bar: no status bar on Android --------------------------------- */

void
ui_sb_update_icon_state(int tag, int state)
{
    (void) tag;
    (void) state;
}

void
ui_sb_update_icon(int tag, int active)
{
    (void) tag;
    (void) active;
}

void
ui_sb_update_icon_write(int tag, int active)
{
    (void) tag;
    (void) active;
}

void
ui_sb_update_icon_wp(int tag, int state)
{
    (void) tag;
    (void) state;
}

void
ui_sb_update_tip(int meaning)
{
    (void) meaning;
}

void
ui_sb_update_panes(void)
{
}

void
ui_sb_update_text(void)
{
}

void
ui_sb_set_text(char *wstr)
{
    (void) wstr;
}

void
ui_sb_bugui(char *str)
{
    (void) str;
}

void
ui_sb_set_ready(int ready)
{
    (void) ready;
}

void
ui_sb_mt32lcd(char *str)
{
    (void) str;
}

void
ui_hard_reset_completed(void)
{
}

void
ui_update_force_interpreter(void)
{
}

static char window_title[512] = EMU_NAME;

char *
ui_window_title(char *str)
{
    if (!str)
        return window_title;
    strncpy(window_title, str, sizeof(window_title) - 1);
    window_title[sizeof(window_title) - 1] = '\0';
    LOGI("title: %s", window_title);
    return str;
}

void
ui_init_monitor(int monitor_index)
{
    (void) monitor_index;
}

void
ui_deinit_monitor(int monitor_index)
{
    (void) monitor_index;
}

/* --- video sink ------------------------------------------------------------ */

static android_frame_cb_t frame_sink      = NULL;
static void              *frame_sink_user = NULL;
static int                geom_w          = 0;
static int                geom_h          = 0;

/* Scratch buffer for the row-copy, matching sdl_render.c's 2048-wide cap
 * (video.h never produces a wider active picture than that). */
static uint32_t frame_buf[2048 * 2048];

void
android_video_set_sink(android_frame_cb_t cb, void *user)
{
    frame_sink      = cb;
    frame_sink_user = user;
}

void
android_video_get_geometry(int *width, int *height)
{
    if (width)
        *width = geom_w;
    if (height)
        *height = geom_h;
}

static void
android_blit_shim(int x, int y, int w, int h, int monitor_index)
{
    if ((x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL)) {
        video_blit_complete_monitor(monitor_index);
        return;
    }

    for (int row = 0; row < h; ++row)
        memcpy(&frame_buf[row * w], &buffer32->line[y + row][x], (size_t) w * sizeof(uint32_t));

    geom_w = w;
    geom_h = h;

    if (frame_sink)
        frame_sink(frame_buf, w, h, frame_sink_user);

    video_blit_complete_monitor(monitor_index);
}

void
plat_resize(int w, int h, int monitor_index)
{
    (void) monitor_index;
    geom_w = w;
    geom_h = h;
}

void
plat_resize_request(int w, int h, int monitor_index)
{
    plat_resize(w, h, monitor_index);
}

/* --- audio sink ------------------------------------------------------------- */

static android_audio_cb_t audio_sink      = NULL;
static void              *audio_sink_user = NULL;

void
android_audio_set_sink(android_audio_cb_t cb, void *user)
{
    audio_sink      = cb;
    audio_sink_user = user;
}

/* android_audio.c calls this from givealbuffer(); kept here so android_ui.c
 * is the single owner of the audio_sink pointer's lifetime. */
void
android_ui_deliver_audio(const int16_t *samples, int nframes)
{
    if (audio_sink)
        audio_sink(samples, nframes, audio_sink_user);
}

/* --- blit mutex + start/stop lifecycle -------------------------------------- */

static mutex_t *blit_mutex = NULL;
static thread_t *main_thread_handle = NULL;
volatile int      cpu_thread_run    = 1;

/* TEMPORARY: total ns spent blocked acquiring blit_mutex in startblit(),
 * since main_thread_func's last read -- isolates blit-mutex contention from
 * genuine cpu_exec() cost in the per-pc_run() timing budget (only the CPU
 * thread calls startblit(), so a plain uint64 is fine, no atomics needed). */
uint64_t g_startblit_wait_ns = 0;

void
startblit(void)
{
    if (blit_mutex) {
        const uint64_t t0 = plat_timer_read();
        thread_wait_mutex(blit_mutex);
        g_startblit_wait_ns += plat_timer_read() - t0;
    }
}

void
endblit(void)
{
    if (blit_mutex)
        thread_release_mutex(blit_mutex);
}

static void
main_thread_func(void *param)
{
    (void) param;

    /* This thread paces pc_run(), which drives PIT/PC-speaker timing and
     * sound_poll() -- the audio ring's only producer (see push_audio() in
     * msdos_host.cpp). It was running at the default (normal) scheduling
     * priority while the Kotlin-side consumer thread that drains the ring
     * (AudioOutput.kt's feeder) runs at THREAD_PRIORITY_URGENT_AUDIO (-19).
     * Raise it to THREAD_PRIORITY_URGENT_DISPLAY (-8) -- the same value the
     * other FujiNet Go targets' combined CPU+audio emulator thread uses (see
     * e.g. fujinet-go-coco's session_runtime.cpp) -- staying below the
     * dedicated audio feeder's -19 rather than matching it, since this
     * thread also does the bulk of the CPU emulation work, not just audio.
     * setpriority() on your own tid needs no special permission for this
     * range -- the Kotlin side already relies on the same fact. */
    setpriority(PRIO_PROCESS, 0, -8);

    /* Measured (see the throughput log below): the original millisecond-
     * granularity version of this loop -- plat_get_ticks() (whole ms) plus
     * plat_delay_ms(1) whenever caught up -- sustained only ~800 pc_run()/s
     * against the 1000/s real-time target, with the drawits backlog hitting
     * its 50ms cap and *resetting* (discarding the elapsed real time it
     * represents, not catching up on it) 3-4 times a second. That permanent,
     * accumulating loss is why the guest's virtual clock runs measurably
     * slower than real time (visible as e.g. a slow cursor blink) while
     * AudioTrack keeps draining the audio ring at a fixed *true* 48kHz --
     * the mismatch chronically underruns the ring, worse the longer a
     * session runs, which is what was misread at first as a pitch bug.
     * Android's actual sleep/scheduler wake-up latency for a nominal "1ms"
     * plat_delay_ms() is coarser than 1ms, so a whole-millisecond pacing
     * loop structurally under-delivers iterations regardless of how cheap
     * pc_run() itself is (it is very cheap: XT-class cpu_exec() is a few
     * thousand cycles). Fix: pace on nanoseconds via plat_timer_read()
     * (already CLOCK_MONOTONIC-backed, see android_plat.c) instead of
     * plat_get_ticks()'s whole-millisecond resolution, and idle on a short
     * 200us nanosleep instead of a full 1ms one, so the loop re-checks far
     * more often and doesn't overshoot the virtual-time budget as much. */
    const int64_t unit_ns        = (force_10ms ? 10 : 1) * 1000000LL;
    const int64_t max_backlog_ns = 50LL * 1000000LL;

    uint64_t old_time_ns = plat_timer_read();
    int64_t  drawits_ns  = 0;

    /* TEMPORARY: measure actual pc_run() throughput against the ~1000/s
     * (or ~100/s under force_10ms) it needs to hold real-time speed, and how
     * often/how far the drawits backlog resets (a hard desync, not just
     * catch-up) -- to confirm the fix against a real number instead of
     * guessing. */
    uint64_t log_time_ns   = old_time_ns;
    int      run_count     = 0;
    int      reset_count   = 0;
    uint64_t pc_run_ns_sum = 0;
    uint64_t pc_run_ns_max = 0;

    /* TEMPORARY: also read the guest's own BIOS tick counter (0x0040:0x006C,
     * physical 0x0000046C) -- incremented ~18.2065/s by the guest's own IRQ0
     * handler, real-mode-visible ground truth for "how fast does the guest
     * think time is passing," and what cursor-blink timing derives from.
     * This catches a guest-clock bug that a pc_run()-call-rate measurement
     * alone would miss (e.g. cpu_exec() not delivering the requested cycle
     * count some other way). */
    uint32_t last_bios_tick = mem_readl_phys(0x0000046CUL);

    while (!is_quit && cpu_thread_run) {
        const uint64_t new_time_ns = plat_timer_read();
        drawits_ns += (int64_t) (new_time_ns - old_time_ns);
        old_time_ns = new_time_ns;

        if ((drawits_ns > 0) && !dopause) {
            drawits_ns -= unit_ns;
            if (drawits_ns > max_backlog_ns) {
                drawits_ns = 0;
                ++reset_count;
            }
            const uint64_t call_start_ns = plat_timer_read();
            pc_run();
            const uint64_t call_ns = plat_timer_read() - call_start_ns;
            pc_run_ns_sum += call_ns;
            if (call_ns > pc_run_ns_max)
                pc_run_ns_max = call_ns;
            ++run_count;
        } else {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000 }; /* 200us */
            nanosleep(&ts, NULL);
        }

        if ((new_time_ns - log_time_ns) >= 1000000000ULL) {
            const uint32_t bios_tick    = mem_readl_phys(0x0000046CUL);
            const uint32_t bios_tick_dt = bios_tick - last_bios_tick; /* wraps fine, unsigned */
            last_bios_tick = bios_tick;

            LOGI("main_thread_func: pc_run=%d/s (target %d/s) resets=%d drawits=%lldns "
                 "pc_run avg=%lldns max=%lldns busy=%lld%% blitwait=%lldns (%lld%% of pc_run time) "
                 "bios_ticks=%u/s (target ~18/s, %d%%) bios_tick_raw=%u",
                 run_count, force_10ms ? 100 : 1000, reset_count,
                 (long long) drawits_ns,
                 run_count ? (long long) (pc_run_ns_sum / run_count) : 0LL,
                 (long long) pc_run_ns_max,
                 (long long) (pc_run_ns_sum / 10000000LL),
                 (long long) g_startblit_wait_ns,
                 pc_run_ns_sum ? (long long) (g_startblit_wait_ns * 100 / pc_run_ns_sum) : 0LL,
                 bios_tick_dt, (int) (bios_tick_dt * 100 / 18), bios_tick);
            log_time_ns    = new_time_ns;
            run_count      = 0;
            reset_count    = 0;
            pc_run_ns_sum  = 0;
            pc_run_ns_max  = 0;
            g_startblit_wait_ns = 0;
        }
    }
    is_quit = 1;
}

void
do_start(void)
{
    is_quit = 0;
    cpu_thread_run = 1;
    if (!blit_mutex)
        blit_mutex = thread_create_mutex();
    video_setblit(android_blit_shim);
    main_thread_handle = thread_create(main_thread_func, NULL);
}

void
do_stop(void)
{
    is_quit = 1;
    cpu_thread_run = 0;
    if (main_thread_handle) {
        thread_wait(main_thread_handle);
        main_thread_handle = NULL;
    }
    video_setblit(NULL);
    pc_close(NULL);
}
