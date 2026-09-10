<!-- BEGIN treecode (auto) — do not edit inside this block -->
# host — POSIX platform layer and the station daemon (`cwnetd`)

Responsibility: Everything the CWNet server side needs that is not the pure protocol
core. Owns a small POSIX platform layer (non-blocking TCP sockets, a monotonic clock)
and `cwnetd`, the daemon that drives `components/keyer_cwnet`'s host-only server core
(`cwnet_server.c`, `cwnet_play.c`, KTD1/KTD2) against a real socket and a real clock. It
must NOT duplicate protocol knowledge — the wire format, the state machine and the
playback timing live in `keyer_cwnet` and are only linked in here, by path, in
`host/CMakeLists.txt`.

Key abstractions:
- `platform/sock.h` / `sock.c` — non-blocking TCP sockets (`listen`/`accept`/`connect`/
  `send`/`recv`/`poll`), POSIX today, written with a winsock seam in its names
  (`sock_valid`, `sock_would_block`, ...) so a Windows port can implement the same
  functions later without touching a caller.
- `platform/clock.h` / `clock.c` — monotonic milliseconds as 64 bits
  (`CLOCK_MONOTONIC`), plus the 31-bit wire width the ping timestamps carry.
  Independent of `esp_timer` and of the `test_host` stubs.
- `cwnetd/main.c` — the daemon: flag parsing, one thread with a `sock_poll()` loop that
  waits for the *next* deadline the core names (not a fixed tick), wiring the server
  core to the platform layer and to `key_output`, status lines on non-blocking stdout.
- `cwnetd/key_output.h` / `key_output.c` — the seam behind which key and PTT edges
  leave the daemon (R11): a struct of function pointers with one backend today,
  `virtual`, which prints `key <0|1> <at_ms>` / `ptt <0|1> <at_ms>` lines.
- `tests/loopback_test.c` — exercises `platform/` and nothing else: a real loopback
  connection, a short write resumed, a remote close seen through the poll, the clock's
  monotonicity and unit. The server core is proven in `test_host/`, against the
  capture; the daemon end to end is proven by hand (`cwnetd/README.md`).

Depends on: `components/keyer_cwnet`'s host-safe sources (`cwnet_frame.c`,
`cwnet_ping.c`, `cwnet_timestamp.c`, `cwnet_play.c`, `cwnet_server.c`), listed by hand in
`host/CMakeLists.txt` because they are deliberately absent from the ESP-IDF component's
`SRCS` (KTD2: host-only). Nothing here is part of the firmware build.

Used by: the maintainer's own station PC, run by hand or (later) under systemd/launchd
— no unit files yet, that is Deferred work. `tools/cwnet/` (a separate module) exercises
`cwnetd` as a client over the loop, without a box.

What is NOT here, and why:
- **No GUI.** How the operator sees daemon state is Decision #65, labelled `blocking`
  for the GUI only. State goes to stdout as lines so that option stays open; do not add
  a TUI, a served page, or a native window here until #65 is decided.
- **No physical key/PTT backend.** `key_output` has one backend, `virtual`, on stdout.
  A serial-line or GPIO backend behind the same two function pointers needs its own
  Decision (KTD9) — not filed yet — because the rest-state and key-down-ceiling
  guarantees on a real transmitter have to be decided before code claims them.
- **No Windows build of the daemon.** `platform/sock.c`'s winsock seam is written to
  make one possible, but nothing here builds it; the host Windows client is separate
  work (issue #68).

Conventions: Same strict flags as `test_host/CMakeLists.txt` (`-Wall -Wextra -Werror
-Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2 -Wnull-dereference`), one
bar for Linux and macOS both. `cwnetd/main.c` owns stdout and protocol knowledge stays
out of it (KTD10: the core does not log) — it carries bytes in, edges out, and prints
what the core reports.

Gotchas:
- `cwnet_client.c` is intentionally NOT linked into `cwnetd`: the daemon is the other
  end of the wire (KTD10), not another client.
- CI's `host-build` job (`.github/workflows/host-tests.yml`) builds and runs only this
  tree; it needs neither the `keyer_iambic` submodule nor its deploy key, which is why
  it is a job of its own rather than more steps on the existing `host-tests` job.
- Build and test locally: `cmake -S host -B host/build && cmake --build host/build &&
  ctest --test-dir host/build --output-on-failure`.
<!-- END treecode (auto) -->
