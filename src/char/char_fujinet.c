/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FujiNet Bus-over-IP (BoIP) character device. Bridges an
 *          emulated COM port straight to a FujiNet-PC BoIP listener over
 *          TCP, byte for byte, so a FUJINET.SYS driver running in the
 *          guest can talk to FujiNet without any host-side PTY plumbing.
 *
 *
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/plat_netsocket.h>

#define FUJINET_DEFAULT_HOST "127.0.0.1"
#define FUJINET_DEFAULT_PORT 1987

/* SLIP frame delimiter (RFC 1055) -- fujinet-msdos/sys/portio.asm's
 * SLIP_END and fujinet-firmware's FujiBusPacket.h's SLIP_END agree on the
 * same value. */
#define SLIP_END_BYTE 0xC0

#ifdef ENABLE_CHAR_FUJINET_LOG
int char_fujinet_do_log = ENABLE_CHAR_FUJINET_LOG;

static void
char_fujinet_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_fujinet_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_fujinet_log(priv, fmt, ...)
#endif

enum {
    FUJINET_STATE_DISCONNECTED = 0,
    FUJINET_STATE_CONNECTING,
    FUJINET_STATE_CONNECTED
};

typedef struct {
    void        *log;
    char_port_t *port;

    SOCKET   sock;
    int      state;
    uint32_t next_attempt;

    /* Snapshotted at init() time - device_get_config_*() relies on a
     * device context that is only valid during device init/close, not
     * from this device's own read()/write() callbacks invoked later by
     * the serial port's receive/transmit timers. */
    char     host[256];
    int      port_num;

    /* Byte-rate pacing: serial.c's receive_timer re-arms itself at the bit
     * period, not the byte period (src/device/serial.c's own comment:
     * "/ * dev->bits * / dev->transmit_period" -- the multiply is disabled),
     * so it polls chardev.read() roughly 10x more often than one byte takes
     * to arrive over a real 8N1 link. That's harmless for a real serial
     * peripheral, which only ever has a fresh byte ready once per true wire
     * byte-period regardless of how often it's asked. A TCP socket has no
     * such natural pacing: an entire reply can already be sitting in the
     * kernel receive buffer the instant BoIP delivers it, so an unthrottled
     * read() here drains it ~10x faster than the configured baud allows --
     * overrunning the emulated UART's single-byte receive register (each
     * new byte silently clobbers the last, since 8250 non-FIFO mode has no
     * hardware flow control) faster than the guest can poll LSR and read it.
     * Pace delivery to one byte per real byte-period (from the port's live
     * baud/format, defaulting to 115200 8N1 before the guest configures the
     * UART) so this device behaves like the real, naturally-paced serial
     * hardware serial.c's receive_timer was designed around. */
    uint64_t next_byte_ns;

    /* Every FujiBusPacket starts with a SLIP_END sync byte, and the guest's
     * own decoder (port_getbuf_slip_dual.asm) explicitly tolerates repeated
     * leading SLIP_END bytes for exactly this reason: the very first byte
     * of a session is uniquely vulnerable to a guest-side cold-start race --
     * fuji_bus_call()'s own setup code runs before the DOS driver's polling
     * loop ever looks at LSR, and that startup latency can outlast several
     * byte-periods, so the first paced byte(s) can go unread and get
     * silently clobbered (8250 non-FIFO has no flow control) before the
     * guest ever polls. Once that leading SLIP_END is lost, the guest's
     * sync phase has no frame start to lock onto until the *closing*
     * SLIP_END, by which point there's no data left -- a complete,
     * checksum-valid packet that can never be parsed. Hold the leading
     * SLIP_END for a real-time guard window, not just one extra
     * byte-period, so it's still there whenever the guest actually starts
     * polling. */
    uint64_t resend_slip_end_until_ns;
} char_fujinet_t;

/* Guard window: how long to keep re-offering the leading SLIP_END before
 * letting the real frame content proceed. Comfortably longer than any
 * plausible fuji_bus_call() setup latency, negligible next to FUJICOM's own
 * 15-second reply timeout (TIMEOUT_SLOW, sys/fujicom.c). */
#define SLIP_END_GUARD_NS (50ULL * 1000ULL * 1000ULL) /* 50ms */

/* Safety margin over the nominal wire byte-period: three layers of real-time
 * pacing are stacked here (this gate, 86Box's own CPU-cycle-driven serial
 * timer, and the Android platform loop that paces CPU emulation to real
 * time in the first place), and each has some scheduling jitter. FUJICOM's
 * decode loop (port_getbuf_slip_dual.asm) does real work between bytes --
 * SLIP-unescaping, buffer writes, loop bookkeeping -- so pacing at exactly
 * the nominal 115200-baud byte period leaves it no slack: an occasional
 * slightly-late poll silently overwrites the byte it was about to read (no
 * hardware flow control in 8250 non-FIFO mode). Bandwidth is not a
 * constraint here -- even a ~150-byte reply at several times the nominal
 * period still finishes in well under a second, against FUJICOM's
 * 15-second reply timeout (TIMEOUT_SLOW, sys/fujicom.c) -- so pace
 * generously rather than chase exact 115200-baud realism.
 *
 * This device has no way to know whether the guest has actually drained the
 * byte currently latched in the UART's single-byte, non-FIFO receive
 * register (src/device/serial.c's serial_receive_timer() overwrites it
 * unconditionally once we hand over a new one -- accurate to how a real
 * 8250 behaves on overrun, but it means *some* fixed real-time gap between
 * bytes is the only guard this device can offer against a guest poll that's
 * occasionally a bit later than usual, e.g. from Android scheduler jitter
 * on the emulator's own host thread).
 *
 * IMPORTANT: this margin applies to every byte this device ever sends, for
 * the lifetime of the connection -- not just the vulnerable cold-start
 * bytes the SLIP_END guard window above exists for. Inflating it trades
 * throughput directly (at SLIP_PACING_MARGIN=300 this device delivered a
 * real ~38 bytes/sec, turning anything beyond a small fixed-size reply --
 * e.g. a directory listing read through a FujiNet-mounted drive, many BoIP
 * round trips of real payload, not one ~150-byte GET_ADAPTERCONFIG reply --
 * into a multi-minute stall), and it turned out to be the wrong tool for
 * the job: real-hardware testing at low margins (6x-8x, no better than
 * plain 8250) still failed ~40% of the time even with the guard window
 * covering the leading SLIP_END, meaning ordinary Android scheduler jitter
 * was dropping bytes throughout a transfer, not just the first one -- and
 * this device has no way to buffer against that on its own.
 *
 * The real fix is serial.c's serial_standalone_init() now handing out a
 * 16550 instead of an 8250: its 16-byte hardware receive FIFO absorbs that
 * jitter directly (FUJINET.SYS's own UART probe, fujinet-msdos/sys/
 * id8250.c, autodetects 16550A and enables the FIFO itself, no driver
 * changes needed), so this margin's job goes back to what it should always
 * have been -- a small cushion over the nominal wire rate, not a stand-in
 * for a receive buffer. */
#define SLIP_PACING_MARGIN 4.0

/* Real nanoseconds per byte at the port's current baud/format (10 bits/byte
 * for the universal 8N1 case; close enough for other formats too), inflated
 * by SLIP_PACING_MARGIN -- see its comment. */
static uint64_t
char_fujinet_byte_period_ns(char_fujinet_t *dev)
{
    uint32_t baud = (dev->port && dev->port->com.baud) ? dev->port->com.baud : 115200;
    return (uint64_t) ((SLIP_PACING_MARGIN * 10.0 * 1e9) / (double) baud);
}

static void
char_fujinet_disconnect(char_fujinet_t *dev)
{
    if (CHAR_FD_VALID(dev->sock)) {
        plat_netsocket_close(dev->sock);
        dev->sock = (SOCKET) -1;
    }
    dev->state = FUJINET_STATE_DISCONNECTED;
    char_update_status(dev->port);
}

/* Attempt (re)connection, throttled to once per CHAR_RECONNECT_MS. Safe to
 * call on every read()/write() poll - it's a no-op unless a retry is due. */
static void
char_fujinet_poll_connect(char_fujinet_t *dev)
{
    uint32_t now = plat_get_ticks();

    switch (dev->state) {
        case FUJINET_STATE_DISCONNECTED:
            if ((now - dev->next_attempt) >= CHAR_RECONNECT_MS || dev->next_attempt == 0) {
                dev->sock = plat_netsocket_create(NET_SOCKET_TCP);
                if (CHAR_FD_VALID(dev->sock)) {
                    if (plat_netsocket_connect(dev->sock, dev->host, (unsigned short) dev->port_num) == 0) {
                        dev->state = FUJINET_STATE_CONNECTING;
                        char_fujinet_log(dev->log, "Connecting to %s:%d\n", dev->host, dev->port_num);
                    } else {
                        char_fujinet_log(dev->log, "connect() to %s:%d failed\n", dev->host, dev->port_num);
                        plat_netsocket_close(dev->sock);
                        dev->sock = (SOCKET) -1;
                    }
                }
                dev->next_attempt = now;
            }
            break;

        case FUJINET_STATE_CONNECTING: {
            int connected = plat_netsocket_connected(dev->sock);
            if (connected == 1) {
                dev->state = FUJINET_STATE_CONNECTED;
                char_fujinet_log(dev->log, "Connected\n");
                char_update_status(dev->port);
            } else if (connected == -1) {
                char_fujinet_log(dev->log, "Connection attempt failed\n");
                char_fujinet_disconnect(dev);
                dev->next_attempt = now;
            }
            break;
        }

        case FUJINET_STATE_CONNECTED:
        default:
            break;
    }
}

static size_t
char_fujinet_read(uint8_t *buf, size_t len, void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    char_fujinet_poll_connect(dev);

    if (dev->state != FUJINET_STATE_CONNECTED)
        return 0;

    /* Not due for a new byte yet - see the pacing comment on next_byte_ns.
     * Deliberately leave any waiting bytes queued in the kernel socket
     * buffer rather than draining them into our own buffer here; TCP holds
     * them safely, and it keeps this function's only job to be "how many
     * bytes are we allowed to hand over right now". */
    uint64_t now = plat_timer_read();
    if (now < dev->next_byte_ns)
        return 0;

    if (len < 1)
        return 0;

    if (now < dev->resend_slip_end_until_ns) {
        /* Still inside the leading-SLIP_END guard window - keep re-offering
         * it instead of consuming a new socket byte, in case the guest
         * hasn't started polling yet. See the guard-window comment above. */
        buf[0]             = SLIP_END_BYTE;
        dev->next_byte_ns   = now + char_fujinet_byte_period_ns(dev);
        return 1;
    }

    int wouldblock = 0;
    int ret         = plat_netsocket_receive(dev->sock, buf, 1, &wouldblock);
    if (ret < 0) {
        if (!wouldblock) {
            char_fujinet_log(dev->log, "recv() error, disconnecting\n");
            char_fujinet_disconnect(dev);
        }
        return 0;
    }
    if (ret == 0) {
        /* Peer closed the connection. */
        char_fujinet_log(dev->log, "Peer closed connection\n");
        char_fujinet_disconnect(dev);
        return 0;
    }

    dev->next_byte_ns = now + char_fujinet_byte_period_ns(dev);
    if (buf[0] == SLIP_END_BYTE)
        dev->resend_slip_end_until_ns = now + SLIP_END_GUARD_NS;

    return (size_t) ret;
}

static size_t
char_fujinet_write(uint8_t *buf, size_t len, void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    if (dev->state != FUJINET_STATE_CONNECTED)
        return 0;

    int wouldblock = 0;
    int ret         = plat_netsocket_send(dev->sock, buf, (unsigned int) len, &wouldblock);
    if (ret < 0) {
        if (!wouldblock) {
            char_fujinet_log(dev->log, "send() error, disconnecting\n");
            char_fujinet_disconnect(dev);
        }
        return 0;
    }

    return (size_t) ret;
}

static uint32_t
char_fujinet_status(void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    if (dev->state == FUJINET_STATE_CONNECTED)
        return CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_DCD;

    return CHAR_DISCONNECTED;
}

static void
char_fujinet_control(uint32_t flags, void *priv)
{
    /* FujiNet's RS-232 wire protocol is fully framed (SLIP) and needs no
     * DTR/RTS handshaking over TCP - see FUJICOM-Protocol.md. Nothing to
     * do here; kept only to satisfy the char_device_t contract. */
    (void) flags;
    (void) priv;
}

static void
char_fujinet_close(void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    char_fujinet_disconnect(dev);
    log_close(dev->log);
    free(dev);
}

static void *
char_fujinet_init(const device_t *info)
{
    char_fujinet_t *dev = (char_fujinet_t *) calloc(1, sizeof(char_fujinet_t));

    dev->sock  = (SOCKET) -1;
    dev->state = FUJINET_STATE_DISCONNECTED;

    /* device_get_config_*() only works while the device context set up by
     * device_add_inst() is active, i.e. right here in init() - snapshot
     * what we need before it goes away. */
    strncpy(dev->host, device_get_config_string("host"), sizeof(dev->host) - 1);
    dev->port_num = device_get_config_int("port");

    dev->port = char_attach(0, char_fujinet_read, char_fujinet_write, char_fujinet_status,
                             char_fujinet_control, NULL, dev);
    dev->log  = char_log_open(dev->port, "FujiNet");
    char_fujinet_log(dev->log, "init()\n");

    char_update_status(dev->port);

    return dev;
}

// clang-format off
static const device_config_t char_fujinet_config[] = {
    {
        .name           = "host",
        .description    = "Host",
        .type           = CONFIG_STRING,
        .default_string = FUJINET_DEFAULT_HOST,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name        = "port",
        .description = "Port",
        .type        = CONFIG_SPINNER,
        .default_int = FUJINET_DEFAULT_PORT,
        .file_filter = NULL,
        .spinner     = { .min = 1, .max = 32767, .step = 1 },
        .selection   = { { 0 } },
        .bios        = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_fujinet_com_device = {
    .name          = "FujiNet (BoIP)",
    .internal_name = "fujinet",
    .flags         = DEVICE_COM | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = char_fujinet_init,
    .close         = char_fujinet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_fujinet_config
};
