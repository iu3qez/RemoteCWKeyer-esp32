---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T09:12:54Z"
title: "The CWNet client track against the reference: six issues closed, the PTT understood, our server in Decision"
summary: "Session September 7-8, 2026: TX MORSE with the reference's stopwatch, feed from the stream, RX MORSE, peak-hold, echo server, PTT as a mirror of the local PTT; read in the source what the DL4YHF server does and opened #60."
keywords: ["cwnet", "dl4yhf", "morse", "set_ptt", "echo-server", "issue-25", "issue-57", "issue-60", "feed", "stream-time"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/handoff-non-affrontato-d8e0f0"
resume_focus: "#25 with the choice \"key anyway and let the box signal it\"; the console command that empties the RX FIFO to close the determinism loop on the link; the free half of #57. Our own server only after the Decision #60."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "cd11e08"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/handoff-non-affrontato-d8e0f0"
---

# The CWNet client track against the reference

Resumed from `2026-09-07_1821_keyer-split-esp32keyertest.md`: the maintainer chose to
work here on the main repo, in parallel with the keyer repo. Six PRs merged, all with CI
green (host tests in both variants and `firmware-build`): **#56** (#10), **#58** (#55), **#59** (#11,
#12), **#61** (#14), **#62** (#13). Host suite from 176 to 205 tests. Nothing proven on
hardware: there's no ESP-IDF on this machine.

## What exists now, and where

- **TX MORSE** in `components/keyer_cwnet/src/cwnet_client.c`: `send_morse()`,
  `cwnet_client_send_key_event(client, key_down, at_ms)`,
  `cwnet_client_poll(client, now_ms, dot_ms)`. The DL4YHF client rule read in
  `KeyerThread.c:2707-2762`: first edge of an over gets zero wait, the stopwatch advances
  by the **encoded** ms, split beyond 1165 ms, second key-up after 14 dots. Frames up to
  128 bytes with a re-base on the edge beyond. `abort_over()` closes the thread on
  overrun or a failed send.
- **Feed from the stream** in `cwnet_feed.c`: CWNet's own best-effort consumer, stream
  time counted in ticks (1 per sample, N per silence marker), end-of-over on aged time
  with the clock sampled right before the drain, 64 edges per pass. The socket layer
  drops the session on a partial `send()`. `bg_task` no longer forwards anything.
- **RX MORSE**: a 128-byte raw FIFO, `cwnet_client_rx_pop()`, `rx_buffered_ms()`,
  `rx_has_end_of_over()` (two consecutive key-ups). Nobody plays it back: in the
  reference a client discards MORSE and the server never sends any.
- **Latency**: `latency_peak_ms` = peak-hold of the reference, floor included (decision
  in the body of #12).
- **PTT**: `set_ptt 1` after the MORSE byte of the key-down, `set_ptt 0` after
  `timing.ptt_tail_ms` from the `poll`; `RPRT n` kept in `rig_result`. Decision in the
  body of #13.
- **Bench**: `tools/cwnet/cwnet_echo.py` (a minimal server with MORSE echo, PING,
  TX_INFO, RPRT 0), `tools/cwnet/keyer_sim.c` (a simulated KeyerThread on the DL4YHF
  encoder: the source of the synthetic expected values).
- **Fixtures**: `test_host/cwnet_fixtures.h`: CONNECT echo, first over of session 12
  (41 bytes, PTT included, timestamp pinned whole by both client and stream), a
  two-event frame, a synthetic frame.

## What the DL4YHF server says, read line by line

All of it is in `.claude/code-quality.md` and in the bodies of #60, #25 (comment), #13
(comment). In short: delayed playback of `iLatency_ms` = max(measured peak, 250
configured, 50); PTT held 500 ms after the last key-up played back, always; the same
`iTxHangTime_ms` is the watchdog that forces the key up when the FIFO is empty; the key
is taken on the first MORSE byte, released on a 1 s timer that doesn't restart on remote
takeover, so `TX_INFO` flips between the name and "nobody" during the over (23/24
announcements in the capture); `set_ptt` applied on arrival, and for a client it's the
mirror of its own local PTT. The maintainer concluded that the server has to be ours:
**#60**, Decision `blocking`, blocks only the server role.

## Open, and what each one is missing

- **#25**: free/mine/someone-else's model from `TX_INFO` (mine = index >= 1 and name
  equal to our username), "nobody" counting as free only if it holds past the flap;
  recommended choice accepted verbally: key anyway and let the box signal it. Not yet
  written into the body.
- **#57**: core defect, `write_idx` published before the slot and `stream_read` at
  `lag == capacity`; the `>= capacity` half is free, the publication order awaits the
  maintainer.
- **Determinism loop on the link**: missing a way to read the RX FIFO from the box
  (console command `cwnet rx`, `key wait` lines). It's already closed on the host
  (`test_client_round_trip_returns_the_edges_sent`).
- #26, #33, #46, #48, #54, #17, #19: unchanged, with the decisions already noted in the
  bodies.
- Sweep done before this handoff: no condition holds true, nothing closed.

## Pattern that worked

Adversarial review on `opus` with a mandate to falsify, before the PR: on #55 it found
four real holes (failed send ignored, truncation at 8 bytes, clock running ahead,
silent resync) and the core defect. Repeat it on every piece that runs on the box
without proof.

## Vocabulary

The maintainer wants **manipolare / manipolazione** (to key / keying), not English
calques; and doesn't want the Hamlib daemon named: block 0x06 is «la stringa di
controllo radio» (the radio control string).

## Machine-local state, fragile

- DL4YHF source: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9...`),
  extracted into the session scratchpad (evaporates) and into `tools/cwnet/ref/` only in
  the main checkout (gitignored). CRLF files: `grep -a`, `tr -d '\r'`.
- Captures in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/` (session 12 =
  keying).
- Light debt in `.claude/code-quality.md`: stale treecode blocks (`test_host`,
  `keyer_core`, `keyer_cwnet`), the silence-marker clamp at 65.5 s.
