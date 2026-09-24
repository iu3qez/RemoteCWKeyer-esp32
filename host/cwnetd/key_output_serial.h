/**
 * @file key_output_serial.h
 * @brief The serial backend's boundary with the OS, for key_output.c and
 *        the host test. Nothing above key_output.h includes this.
 *
 * KTD8: every call the backend makes to the OS goes through one table of
 * function pointers, set to the real calls in the daemon. The host test
 * replaces it, because a pty cannot stand in for a serial port: Linux ptys
 * refuse the modem-line ioctls. The test proves the order of the calls, the
 * rest state, the mapping and the fault paths; only the bench (U6) proves
 * what a real adapter does with them.
 */
#ifndef HOST_CWNETD_KEY_OUTPUT_SERIAL_H
#define HOST_CWNETD_KEY_OUTPUT_SERIAL_H

#include "key_output.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A line change slower than this is a FAULT: the PTT tail (KTD3, R7). */
#define KEY_OUTPUT_SERIAL_SLOW_US 100000

/** Reads of received bytes per key_output_service(), so a port that never
 *  stops talking cannot hold the loop. */
#define KEY_OUTPUT_SERIAL_MAX_READS 8

typedef struct key_output_os {
    int (*open)(const char *path, int flags);
    int (*close)(int fd);
    /** ioctl with an int argument: TIOCMSET, TIOCMGET. */
    int (*ioctl_int)(int fd, unsigned long request, int *arg);
    /** ioctl without an argument: TIOCEXCL. */
    int (*ioctl_none)(int fd, unsigned long request);
    int (*tcgetattr)(int fd, struct termios *t);
    int (*tcsetattr)(int fd, int action, const struct termios *t);
    int (*flock)(int fd, int operation);
    /** Received bytes, read to be dropped (key_output_service()). */
    ssize_t (*read)(int fd, void *buf, size_t len);
    /** Monotonic microseconds, to time each line change. */
    int64_t (*now_us)(void);
    /**
     * Set the speed to B0 at open when no used line is inverted. Linux
     * only: there it stops later opens from raising the lines (KTD2,
     * tty_port.c:504-507). macOS raises both lines at every open whatever
     * the speed (IOSerialBSDClient.cpp:2401-2408), and hands a rate of 0
     * to a closed driver (iossparam(), PD_E_DATA_RATE), so there B0 gains
     * nothing and risks a tcsetattr that fails at every start.
     */
    bool b0_at_rest;
} key_output_os_t;

/** The real calls. */
extern const key_output_os_t key_output_os_posix;

/**
 * @brief Open @p device and leave it at rest (KTD2).
 *
 * The order is the safety property: open, then both lines to rest in one
 * call, then termios (HUPCL, CLOCAL, raw, no flow control, and B0 as
 * key_output_os_t says),
 * then the exclusive lock, then the lines read back. Any failure closes the
 * descriptor, makes no further line call, and writes a message that names
 * the device.
 *
 * @p out must already carry its line sink; the rest of it is filled here.
 */
bool key_output_serial_open(key_output_t *out, const char *device,
                            const key_output_map_t *map, const key_output_os_t *os,
                            char *err, size_t err_len);

/** The edge line every backend writes: "key 1 123456" (key_output.c). */
void key_output_edge_line(key_output_t *out, const char *what, bool on, int64_t at_ms);

#ifdef __cplusplus
}
#endif

#endif /* HOST_CWNETD_KEY_OUTPUT_SERIAL_H */
