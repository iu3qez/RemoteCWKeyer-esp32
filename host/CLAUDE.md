<!-- BEGIN treecode (auto) — do not edit inside this block -->
# host — POSIX platform layer, the station daemon (`cwnetd`) and its panel

Responsibility: Everything the CWNet server side needs that is not the pure protocol
core: a small POSIX platform layer; `cwnetd`, which drives `components/keyer_cwnet`'s
host-only server core against a real socket and clock; and `panel/`, a separate Python
program that shows the daemon's state in a browser. It must NOT duplicate protocol
knowledge: wire format, state machine and playback timing live in `keyer_cwnet` and are
only linked in, by path, in `host/CMakeLists.txt`.

Key abstractions:
- `platform/sock.h`, `platform/clock.h` — non-blocking TCP with a winsock seam in its
  names, and 64-bit monotonic milliseconds; independent of `esp_timer`.
- `cwnetd/main.c` — one thread, a `sock_poll()` loop that waits for the next deadline
  (the core's or the snapshot's), never a tick. Owns stdout: droppable status lines with
  each loss confessed, `stato ptt` when the output PTT changes, and every `--snapshot-ms`
  the whole state over several lines. The tables in `cwnetd/README.md` are the contract
  with every reader, versioned (`v1`).
- `cwnetd/key_output.[ch]` — the seam key and PTT edges leave through (one backend,
  `virtual`), to the `--edges` descriptor, which never drops a line.
- `panel/cwnetd_panel.py` — joins `follow.py` (stdin, or a file followed like `tail -F`),
  `state.py` (pure model; the guarantee and liveness kept apart) and `web.py` (fixed
  routes, `/events` SSE, Host allow-list). Read-only; never talks to the daemon.

Depends on: `components/keyer_cwnet`'s host-safe sources, listed by hand in
`host/CMakeLists.txt` and absent from the ESP-IDF component's `SRCS`. Nothing here is
part of the firmware build. `panel/` depends only on the status-line vocabulary.

Used by: the maintainer's station PC, run by hand (no systemd or launchd units yet).
`tools/cwnet/` drives `cwnetd` as a client; `cwnet_jitter.py` measures its edges.

What is NOT here, and why:
- **No GUI in the daemon.** Decision #65, closed 2026-09-18: the loop is the timing, so
  the page is `panel/`, another process that reads the lines and commands nothing.
- **No physical key/PTT backend.** A serial or GPIO one needs its own Decision (KTD9),
  not filed yet: rest state and key-down ceiling on a real transmitter come first.
- **No Windows build.** The winsock seam makes one possible; the client is #68.

Conventions: C under the same strict flags as `test_host`, Linux and macOS. `main.c`
owns stdout and the core does not log. A status line changes in the `cwnetd/README.md`
tables first, and the vocabulary version moves when a line changes shape. `panel/` is
Python 3.9+, standard library only; its page text is Italian, everything else English.

Gotchas:
- `cwnet_client.c` is NOT linked into `cwnetd`: the daemon is the other end of the wire.
- The snapshot gives way to a core deadline within 2 ms, never for more than a period:
  its eleven writes must not come before a due edge.
- A status line holds 254 characters. Callsigns are escaped (`\xNN`), and a snapshot
  client line declares the name's length, so a reader tells a cut name from a
  confession glued to half a line.
- `2>&1` into a pipe the panel reads lets a slow panel stall `edge_write()`.
- Tests: `tests/loopback_test.c` covers `platform/` only; `panel/tests/` include a real
  recorded stream with lines removed and a live run against the built `cwnetd`
  (`CWNETD=host/build/cwnetd`). CI jobs `host-build` and `panel-tests` need no submodule.
<!-- END treecode (auto) -->
