/**
 * @file cwnet_fixtures.h
 * @brief Bytes from the 2026-09-05 capture of the official DL4YHF client and
 *        server, shared by the CWNet host suites
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "cwnet_frame.h"
#include "cwnet_client.h"

/*
 * The reference protocol has no WELCOME: 0x00 is CWNET_CMD_NONE, "dummy command
 * to send HTTP instead of our binary protocol" (CwNet.h). A real server confirms
 * a connection by echoing the 92-byte CONNECT record back with the permissions
 * field filled in, then sending a PRINT with the greeting.
 *
 * These are the first 94 bytes of the server-to-client direction of session 10
 * of the 2026-09-05 capture of the official DL4YHF client and server: the
 * CONNECT echo, user "Moritz", permissions 0x07 where the client had sent 0.
 */
static const uint8_t ref_connect_echo[] = {
    0x41, 0x5C, 0x4D, 0x6F, 0x72, 0x69, 0x74, 0x7A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x6F,
    0x72, 0x69, 0x74, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
};


/*
 * Client-to-server bytes 238..278 of session 12 of the 2026-09-05 capture of
 * the official DL4YHF client: its first over, the letter A at 25 WPM (dot
 * 48 ms) and then silence. Five MORSE frames, with the two rigctld set_ptt
 * frames the reference interleaves around an over (PTT is #13; we do not
 * send it, so the comparison is on the MORSE frames alone).
 */
static const uint8_t ref_first_over[] = {
    0x50, 0x01, 0x80,                                     /* key down, wait 0      */
    0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74,
    0x20, 0x31, 0x0A, 0x00,                               /* RIGCTLD "set_ptt 1"   */
    0x50, 0x01, 0x24,                                     /* key up   after  48 ms */
    0x50, 0x01, 0xA4,                                     /* key down after  48 ms */
    0x50, 0x01, 0x3C,                                     /* key up   after 144 ms */
    0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74,
    0x20, 0x30, 0x0A, 0x00,                               /* RIGCTLD "set_ptt 0"   */
    0x50, 0x01, 0x60,                                     /* end of over, 669 ms   */
};

/**
 * @brief Copy the raw bytes of every MORSE frame of a capture slice, in order
 *
 * @return false on a parse error
 */
static inline bool ref_morse_frames(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t *out_len) {
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    size_t off = 0;
    size_t n = 0;
    while (off < in_len) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, in + off, in_len - off);
        if (r.status != CWNET_PARSE_OK) {
            return false;
        }
        if (r.command == CWNET_CMD_MORSE) {
            memcpy(out + n, in + off, r.bytes_consumed);
            n += r.bytes_consumed;
        }
        off += r.bytes_consumed;
    }
    *out_len = n;
    return true;
}
