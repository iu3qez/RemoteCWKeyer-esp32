---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-10T23:14:00Z"
title: "The station daemon, and the plan that wasn't committed"
summary: "Session September 10, 2026: the station daemon's eight units merged in #77, then discovered after the merge that the engine was keying garbage with a live operator; #78 fixes that and the other four entries of a plan revision that lived only in an uncommitted file. What remains: the bench pass (#64), Linux (#76) and an open decision on the idle-network policy."
keywords: ["station-daemon", "cwnetd", "cwnet_play", "cwnet_server", "cwnet_rxfifo", "underrun", "grace", "buffer-floor", "edges-descriptor", "issue-64", "issue-65", "issue-68", "issue-76", "pr-77", "pr-78", "plan-provenance", "live-keying"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/colour-rule-key-verdict-led-da2126"
resume_focus: "PR #78 is waiting on the maintainer's merge. After that: the open decision on the idle-network policy (R6), the bench pass with the box that keeps #64 open, and #68's host client, which reuses host/platform/ and cwnet_rxfifo.h."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "fix/64-the-element-holds"
head: "fed9073"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/colour-rule-key-verdict-led-da2126"
---

# The station daemon, and the plan that wasn't committed

Resumed from `2026-09-09_2054_colour-rule-key-verdict-led-pruning.md`, which pointed to
the station side as the next step. The station side now exists:
[#77](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/77) merged in `606d39e`,
[#78](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/78) open and green, waiting on
the maintainer.

The central fact of the session isn't the code: it's that **a plan revision existed for
twenty hours in a single uncommitted file**, and in the meantime #77 implemented the old
text. The defect that came out of it was serious. Whoever reads this handoff should read
the "Where the plan came from" section first.

## What exists now, and where

- **The station keys.** `host/cwnetd/` is the daemon: one thread, `sock_poll()` with a
  timeout at the next deadline, never a fixed tick. `host/platform/` is the POSIX layer,
  with winsock-style naming so the #68 host client can reuse it.
- **The core is pure and host-only.** `components/keyer_cwnet/cwnet_server.[ch]` takes a
  client from CONNECT to READY, arbitrates the key and announces the holder;
  `cwnet_play.[ch]` turns MORSE bytes into edges. Neither reads the clock or prints:
  time is a parameter, events come back in a struct. They're deliberately outside the
  ESP-IDF component's SRCS, and are listed by hand in `host/CMakeLists.txt` and
  `test_host/CMakeLists.txt`.
- **A single FIFO for both ends**: `components/keyer_cwnet/include/cwnet_rxfifo.h`,
  header-only. The timestamps stay separate on purpose, and the why is in the header:
  the box stamps with the reference's wrapping 31-bit counter, the daemon with a
  64-bit monotonic instant.
- **The wire's proofs** are in `test_host/test_cwnet_server.c` and `test_cwnet_play.c`,
  pinned to `test_host/cwnet_fixtures.h`, i.e. the 2026-09-05 capture.
- **The bench procedure and the numbers** are in `host/cwnetd/README.md`. The turnaround
  time after TX is 200 ms measured, not calculated.

## Where the plan came from, which is the thing not to repeat

The plan `docs/plans/2026-09-08-2158-feat-station-daemon-plan.md` existed in two
versions. The one #77 executed was the copy on disk at 23:01 on September 9. A more
recent revision, written at 01:30 on the 10th, lived **only** as an untracked file in
the `station-daemon-startup-03165a` worktree: no commit, no branch, and
`git log --all -S` found no line of its text.

Eleven entries differed, and not in form: the underrun rule, the buffer floor, the edge
descriptor, the FIFO extraction. #78 commits it **as the first commit** (`37b8364`)
precisely to close that hole: authority has to be in git before the code answers to it.

How it was discovered: at the end of the session, while checking whether that worktree
still held anything unique before proposing to clean it up. If I hadn't checked, the
revision would have been deleted along with the worktree.

**On attribution, and this is my reading, not an established fact.** The maintainer said
he made those changes himself. Another Claude session, the `ce-plan` in that worktree,
later wrote that it had written them between 21:00 and 22:00 on the 10th, after the #77
merge. The filesystem says the file hasn't been touched since 01:30 on the 10th, nine
hours *before* the merge, and the content was already that. The technical content of
that session was instead verified and correct on the two points I checked: its
independent reproduction of the defect, and the `CwNet.c:164` citation on the 250 ms. I
haven't resolved the authorship contradiction and didn't use it to decide anything.

## The defect the merge didn't stop

The engine treated "FIFO empty at an edge's deadline" as underrun. But an operator sends
one byte per edge **as it happens**, so during an element longer than the buffer the
FIFO is empty by construction. Result with the box at the key: the letter A came out as
a correct 48 ms dot followed by a zero-duration flash instead of the dash.

The 303 tests were green because every fixture is delivered in one burst. The test that
tells the two rules apart is
`test_play_an_over_delivered_as_it_is_keyed_plays_like_a_buffered_one`: the same bytes,
one at a time, at their own arrival instant.

Verification performed, and it counts more than the tests: a client keying in real time
against the real daemon. Before the fix, the zero-duration flash; after, dot 48, space
48, dash 144, PTT tail 100, no fault. The script is machine-local, in the session
scratchpad, and wasn't committed: it's twenty lines to rewrite.

## Decisions, and whose they are

- **From the maintainer**: not opening an issue for the parser defect, because it would
  be QRM (so that line of #77's Definition of done stays empty by choice, and the PR
  says so); opening an issue instead for the measurement on Linux, which is #76; the
  eleven entries of the plan revision, according to what he stated.
- **Mine**: the buffer floor expressed in the tests as a constant rather than a number,
  so the next change doesn't go unnoticed; aligning the engine event's name to what it
  now means; the refusal, in review, to extract a generic FIFO - **overturned by the
  plan**, and it was right to overturn it: since both ends now use the same end-of-over
  rules, two copies that diverge become two different behaviours on the same wire.
- **From a worker, approved by me**: between "edges are never dropped" and "the loop
  never stops", the first wins, because those lines *are* the jitter measurement; the
  cost is made visible on a status line instead of hidden.

## What's still open

- **The decision on the idle-network policy (R6).** It measures silence on the wire, not
  in playback: a client that queues more than the timeout gets the key taken away
  mid-transmission. The box can't do that; a client that queues can. It's one line plus
  a test, but it changes what R6 means, so it's station policy. Asked the maintainer
  twice, no answer; it's written under "Not done here" in #78.
- **[#64](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/64) stays open** on the
  bench half. The procedure is in `host/cwnetd/README.md`, "Bench with the box (R18)"
  section. A comment on the issue records that the host half holds up.
- **[#76](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/76)**: jitter is measured
  only on macOS. CI builds and tests `host/` on Ubuntu too, which isn't the same thing.
- **[#65](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/65) stays `blocking`**
  for the daemon's GUI alone, and neither PR contains one.
- **[#68](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/68)**, the host client,
  is the natural next piece: it reuses `host/platform/` and `cwnet_rxfifo.h`, which were
  written with it in mind.

## Local state and traps

- The `station-daemon-startup-03165a` worktree no longer holds anything unique: the plan
  is committed in `37b8364`, and its `CONCEPTS.md` and `parameters.yaml` are identical to
  the ones on `main`. Before this session it was the only copy of the revision.
- **Unity in this repo has no auto-discovery**: a test function that's declared but not
  passed to `RUN_TEST` in `test_host/test_main.c` compiles and never runs. It happened
  twice in this session. When several agents work in parallel, that file is best wired
  by one person.
- `SendMessage` is disabled in this session: no replying to other Claude sessions, only
  going through the maintainer.
- The GitHub MCP server won't connect (`Authorization header is badly formatted`); the
  `gh` CLI works and is what I used.

## Verification performed

Host suite 312/312, clean and with ASan/UBSan. `host/`'s `ctest` green in both variants.
CI green on eighteen checks for #78, `firmware-build` included, which matters because
`cwnet_client.c` changed. Plus the live proof described above.
