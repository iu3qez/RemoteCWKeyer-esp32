---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T09:58:00Z"
title: "Station daemon and host client: strategy decided, #60 closed, #64, #65 and #68 open, zero lines written"
summary: "Session September 8, 2026: STRATEGY.md flipped on the station side (our server in C, daemon on a Linux/Mac PC, DL4YHF reference of the wire and not the product) and then on the client side (portable host program, Windows first target, Mac best effort); #60 closed, Work #64 daemon, Decision #65 blocking for the GUI, Work #68 host client."
keywords: ["cwnet", "daemon", "server", "station", "client", "host", "windows", "serial", "dl4yhf", "gui", "issue-60", "issue-64", "issue-65", "issue-68", "strategy", "keyer_sim", "cwnet_echo"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
resume_focus: "Two independent starting points: the station daemon in C (#64) from the keyer_cwnet codec compiled for host and from the bench's echo server; the portable host client (#68) from cwnet_socket, the only platform file. The GUI waits on Decision #65, blocking only for the GUI."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "c9b0ea1"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
---

# Station daemon and host client: decided, not started

Resumed from `2026-09-08_0912_cwnet-client-track.md`. This session wrote no code: it
changed the strategy twice, closed the Decision that was blocking it, and opened the
three issues the work starts from. Everything is on `main`: PR #63 (station side) and
PR #67 (host client), both merged by the maintainer.

## What the maintainer decided, and where it's written

The decisions are the maintainer's, made in chat on the 8th; the prose is mine.

- **The DL4YHF program is the reference for the wire, not for the product.** Different
  purpose from ours, Borland Windows only, GUI with no source, CI-V tagging along,
  debatable station choices (PTT held 500 ms). `STRATEGY.md`, Purpose and Positioning.
- **The product is our own two ends: the server, and a client that's either the box or a
  program on a PC.** Station policies (PTT, key handover, playback buffer) are ours,
  made once because we own both ends. The box on its own isn't a product.
  `STRATEGY.md`, Positioning (PR #67 for the client part).
- **The server is a daemon on a station PC, Linux or Mac, in C, in this repo, on the
  `keyer_cwnet` codec compiled for host.** Option 3 of #60, against recommendation 2
  (server on the box). Reason: the reference's policies must not enter the box's RT
  path; the remote channel in `sample.h` and playback on Core 0 are no longer needed.
  `STRATEGY.md`, track "CWNet, both ends"; #60, Decision section.
- **Audio, CI-V and spectrum inside CWNet: no commitment.** The daemon implements the
  commands keying requires; the rest of the station goes through whatever it goes
  through today (Thetis). `STRATEGY.md`, Boundaries.
- **No box as server, for now.** A box with a dual client/server personality is a
  useful repurpose for testing too, parked as a boundary. `STRATEGY.md`, Boundaries.
- **A single repo, for now.** My recommendation, accepted: what the two ends share is
  knowledge of the wire, and that lives here (fixtures, captures, tests). The split into
  its own repo comes when the daemon has its own release cadence, the way it happened
  with the keyer.
- **A small GUI with the daemon.** The maintainer's request, technology not chosen:
  that's #65.
- **The client is also a portable host program, Windows first target, Linux after, Mac
  best effort.** A need that came up from a parallel project of the maintainer's; the
  paddle comes in through the control lines of a USB serial port. Windows-only was
  discarded because the host client is also the bench without the box, and the bench
  runs on the Mac where development happens. Mac best effort, and not excluded: macOS
  has no `TIOCMIWAIT` and you poll `TIOCMGET`, but polling at 1 ms is equivalent to the
  events on Windows/Linux, which themselves arise from USB packets at the adapter's
  latency timer; the real limit is that the Apple driver leaves the FTDI latency timer
  at 16 ms, one dot at 40 WPM. `STRATEGY.md`, Users, Boundaries, track "CWNet, both
  ends". The Boundaries line "K8 feeling on the client PC: no metric" is a proposal of
  mine that the maintainer left in.
- **Who uses the Windows client** hasn't been said: Users calls it "the OM who keys from
  a Windows PC". If the parallel project has a specific person or situation, it belongs
  there.

## Tracker

- **#60** closed, `blocking` stays on the issue as it does on every decided Decision.
  The body carries the decision and a "What it blocks" rewritten for option 3. The body
  was already past 300 words before this session; I didn't trim it.
- **#64** Work, opened by me: the daemon. Closing condition: the host suite runs the
  daemon against the DL4YHF client capture (CONNECT, PING, session 12's over in
  `test_host/cwnet_fixtures.h`) and pins its bytes; the box's client completes an over
  against it in the bench loop with `tools/cwnet/keyer_sim.c` as the stimulus. Blocked
  on: nothing.
- **#65** Decision `blocking`, opened by me: which GUI and in what technology. Four
  options in the body (a page on localhost, TUI, native GTK4/nuklear/raygui window, no
  GUI); my recommendation is the TUI with status also on stdout. **Blocks only the
  GUI**, not the daemon core: A Blocking Issue Blocks applies to the GUI and to nothing
  else.
- **#68** Work, opened by me: the host client. Closing condition: a program built by the
  repo's CMake on Windows (MinGW or MSVC) and Linux, paddle from two control lines of a
  serial port at the FSM's 1 ms tick, the submodule's FSM in the stream, `keyer_cwnet`
  on top of a winsock/POSIX socket layer with a monotonic clock; the host suite pins its
  wire with the same test as the box's client, a Windows CI job builds it, and the tick
  jitter on Windows is measured and written down before claiming the K8 metric. Blocked
  on: nothing. Client and daemon (#64) are the two ends of the bench loop without the
  box: they start independently and meet there.
- **#25** closed by another session in parallel (PR #66):
  `cwnet_client_get_key_holder()` and the callsign pinned on the wire. I haven't reread
  it; the body of #25 and the PR say what's there.
- Sweep done before the first version of this handoff, after the merge of #63: no
  condition holds true, nothing closed. Since then #25 has been closed by another
  session; I haven't rechecked #57, #54, #48, #46, #33, #26, #19, #17 after PR #66.

## Where the daemon starts from: what exists

- **Codec and protocol, already compiled for host.** `test_host/CMakeLists.txt:89-93`
  compiles `cwnet_timestamp.c`, `cwnet_frame.c`, `cwnet_ping.c`, `cwnet_client.c`,
  `cwnet_feed.c` without ESP-IDF: proof that the codec holds up under a host build. The
  daemon reuses those files, doesn't copy them. `cwstream_encode_timestamp()` is the
  only function verified exhaustively against the reference
  (`tools/cwnet/diff_main.c`, #33).
- **The daemon's seed**, `tools/cwnet/cwnet_echo.py`: a minimal server in Python with
  CONNECT echo, PING, `TX_INFO`, `RPRT 0`, MORSE echo. It says which commands are
  enough to make the box's client believe it's connected. Worth rereading for the
  sequence, not porting line by line.
- **The stimulus**, `tools/cwnet/keyer_sim.c`: a simulated KeyerThread on the DL4YHF
  encoder, producing the synthetic expected values. Build instructions in
  `tools/cwnet/README.md:57`.
- **The fixtures**, `test_host/cwnet_fixtures.h`: `ref_connect_echo`, `ref_first_over`
  (41 bytes, PTT included), `ref_two_event_frames`, `synth_morse_frame`. The daemon's
  first test reads them from the other end: it receives what the client sends.
- **The PING** from the server side: `.claude/code-quality.md:19-21` explains the three
  timestamp slots and who reads what. The server is the initiator; the difference is
  always computed on its own clock.

## Where the host client starts from: what exists

- The same core as the box's client, already compiled for host in `test_host` (above).
  `cwnet_socket.c` is the only file in `keyer_cwnet` tied to ESP-IDF: BSD sockets plus
  `esp_timer` and FreeRTOS (`components/keyer_cwnet/src/cwnet_socket.c:13-19`). The host
  client needs a winsock/POSIX variant of that file and a monotonic clock, nothing else
  on the protocol side.
- The iambic FSM is in the `keyer_iambic` submodule (Esp32KeyerTest), pure C with a
  frozen interface in `iambic.h` and `sample.h`; the keying stream in `keyer_core` is
  pure C.
- Everything else is missing: paddle input from CTS/DSR (Windows `WaitCommEvent` with
  `EV_CTS|EV_DSR`, Linux `TIOCMIWAIT`, Mac polling of `TIOCMGET`), the 1 ms tick on
  Windows (`timeBeginPeriod(1)` and a high-priority thread, jitter to be measured), the
  sidetone on PC, the CMake for the target and the Windows CI runner. None of this has
  been chosen or written.

## The reference's policies, to decide and not to copy

Read in the DL4YHF source and logged in the body of #60 with `file:line`: delayed
playback of `iLatency_ms` = max(measured peak, 250 configured, 50); PTT held 500 ms
after the last key-up played back, always; the same `iTxHangTime_ms` acts as the
watchdog that forces the key up when the FIFO is empty; the key is taken on the first
MORSE byte and released on a 1 s timer that doesn't restart on remote takeover, hence
the `TX_INFO` flap between the name and "nobody" during the over (session 12, 23/24
announcements); `set_ptt` applied on arrival, not aligned to playback. The box's client
sends `set_ptt 1` after the key-down and `set_ptt 0` after `ptt_tail_ms` (#13): the
daemon is the first place where that PTT gets read for real.

The rule from the STRATEGY: a change that reproduces a DL4YHF station choice **only
because the reference makes it** is to be rejected. Every daemon policy comes from a
choice of ours, written into the body of #64 or into a Decision if it isn't obvious.

## Source and captures, machine-local

- DL4YHF source: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9…`),
  extracted into `tools/cwnet/ref/` only in the main checkout, gitignored. Fetch recipe
  in `tools/cwnet/README.md:35-39`. CRLF files: `grep -a`. Written permission obtained,
  not up for debate; the archive is incomplete and `CwNet.c` doesn't compile on its own.
- Captures: `tmp/oracle/` in the main checkout, not committed. Which ones become
  fixtures is #33.
- The `busy-chaum-c78815` worktree is clean and its branch is merged: it can be
  discarded.

## What wasn't done, on purpose

- No line of the daemon or the host client, no CMake for the host targets, no directory
  choice (`tools/cwnet/` like the bench, or a new directory: to decide at the first PR,
  it isn't a Decision). Daemon and host client share the socket layer and the clock:
  whoever starts first writes them for both.
- No choice on how the daemon keys the rig from the PC (serial, USB, GPIO of an
  adapter): #64 says "key output on the host" and no more. Needed before closing #64;
  it's a Decision if it has more than one reasonable answer.
- The bench plan (`docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md`) stays
  `requirements-only` for #19; its units U5/U6 on the echo server have been overtaken by
  events (the echo exists, #14) and now by the daemon.

## What worked in this session

The maintainer doesn't see the text that precedes a blocking question in the desktop
app: the draft has to go into the final message, and confirmation comes through chat.
Before every choice question: phase, decision, consequences of the options.
