---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-05T20:15:00Z"
title: "Partial CWNet session zero, and the debt workflow rewritten twice"
summary: "Five CWNet hypotheses closed against the real reference (H6 disproved), bench tools in the repo, ten issues opened, two new workflow rules corrected by a review, and two March PRs awaiting a decision."
keywords: ["cwnet", "dl4yhf", "sessione-zero", "oracolo-differenziale", "issue-workflow", "pr-stale"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32"
resume_focus: "Two open things waiting on the user: the decision on March PRs #2 and #3 (analysis ready, not closed), and then the unblocked work - #14 echo server, or #10 TX on MORSE 0x10."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "cwnet-bench-session-zero"
head: "30fbd45"
---

# Partial session zero, and the rewritten workflow

Seven commits on `cwnet-bench-session-zero`, [PR #21](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/21) open and pushed. Working tree clean.

The session did two different things: it closed five CWNet hypotheses against the real reference, and it changed the way work gets tracked. The second part came later, at the user's request, and is the one with more sharp edges.

## How it started, and how it got redirected

The previous handoff said to resume from the evidentiary weight of the fixtures (KTD5). We tried with `ce-brainstorm`, but the user **redirected after a few exchanges**: *«Lascia stare questo. Dobbiamo pensare come testare contro il golden standard DL4YHF» [Leave this aside. We need to think about how to test against the DL4YHF golden standard]*. That brainstorm is **abandoned, not concluded**: the question of how far to carry the distinction between `reference/`, `ours/` and `synthetic/` fixtures remains open and undecided.

## Part 1 - session zero

Full method in [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md), to read before touching CWNet. In short: DL4YHF's `CwStreamEnc.c` is plain C, it compiles with a four-typedef shim, and the input domain is small enough for an **exhaustive** comparison with our codec. Zero mismatches. `CwNet.c` does not compile (missing `StringLib.h`, `yhf_type.h`, `QFile.h`, `YHF_*.h`; the author does not provide them) but it's not needed: our parser reads the bytes.

**DL4YHF's permission exists, by email.** The user got irritated when I brought it back into question after he had already said so. Do not reopen it.

The VM's loopback was not capturable with Npcap, so the traffic went through a **TCP tap** on the Mac. Tools in [tools/cwnet/](../../../../tools/cwnet/); the reference codec is **not** vendored, by the user's explicit choice, and the README has the fetch command.

### Outcomes (full table in the plan, Dependencies section)

| | |
|---|---|
| H1, H2, H4 | confirmed |
| H3 | confirmed **but corrected**: end of over at **14 dot-times**, not "10 dots or 500 ms". The comment in the source is wrong; code and bytes agree |
| H5 | confirmed **with a defect**: the peak-hold uses integer division, it gets stuck at `instantaneous + <10 ms` and never comes back down. Seen in the author's GUI log (`pk = 7 ms` stuck for 90 s with instantaneous 1–5) |
| H6 | **disproved** |
| H7 | not observable without our box on the wire |

Not anticipated: **PTT travels as a rigctld string** (`set_ptt 1/0`) in `0x06` frames; the client **packs** multiple events per frame when it finds them queued; permissions are a bitmask.

### H6 is the fact that changes the plans

The server **does not send the keying back**, and it's not latency (the user had hypothesized that; the source says no). `CWNET_CMD_MORSE` goes out from a single point in `CwNet.c`, and the FIFO that feeds it only fills under `iFunctionality == CWNET_FUNC_CLIENT`: **a server instance never emits MORSE**. A second client is not needed.

R7 loses the bench it assumed. The fallback was foreseen - an echo server of our own - and it's issue #14. It holds: the **determinism** comparison does not need the reference at both ends, the **format** one does and that evidence now exists.

## Part 2 - the workflow, changed and then corrected

The user asked that problems and debts become GitHub issues. The rule was written, **applied immediately**, and produced ten issues in one pass - two of which were QRM closed within the hour, because the first draft said «a problem, a debt, a stub» without drawing any line. The user corrected it twice: first the criterion, then the fact that light debts don't belong on the tracker («sarebbero solo QRM come diciamo noi radioamatori» [they'd just be QRM, as we hams say]).

From there the second rule was born, at his request: **workflow changes go through review**. Both live in CLAUDE.md under Critical Constraints. The **final version** holds, not the intermediate ones.

Then the review (`/code-review`) of the two rules found **eleven real defects out of twelve findings**, all corrected in `30fbd45`. The three that matter:

- «Reviewed before it stands» **had no mechanism**: CLAUDE.md is in force the instant you write it, there is no dormant state. Now the window is the PR, the reviewer is the maintainer, and an agent reviewing its own rule doesn't count.
- The two rules **contradicted each other** on a case already in the repo (the devcontainer pin is at once «wrong version» and «cannot be validated here»). Now the four conditions always win.
- The light-debt escape hatch **bypassed the Definition of done**: an "obvious" fix in `keyer_cwnet/` would have gone through without a host test. Now it's explicit that it doesn't.

One finding **rejected**: it claimed the solution docs have no `tags:`. Both of them have it, verified.

**Deliberately not reconciled**: the duplicates between the tracker and `.claude/feature-status.md` / `code-quality.md` (e.g. #16 and #17). Reconciling them would be applying a fresh rule to the backlog, which is exactly what the new rule forbids until the maintainer approves it. The rule says who wins (the issue); the execution waits.

## State of the work

**Done and verified.** Tools in `tools/cwnet/` (the three C programs compile clean, `difftest` with UBSan, `cwnet_dump` with `-Werror`; exhaustive comparison 0 mismatches). H1–H7 outcomes in the plan. The method learning. `CONCEPTS.md` from scratch, 7 entries. CLAUDE.md points to `docs/solutions/`, `CONCEPTS.md` and `thoughts/shared/handoffs/`. README rewritten for an outside reader (it said the project is in Rust: 0 `.rs` files, 77 `.c`).

**Not done.** The host suite **was not rerun** in this session. No production code was touched, so no regressions are expected, but it's not verified. The firmware cannot be built here (no ESP-IDF installed; the build line in CLAUDE.md was corrected twice and now wants an explicit `IDF_PATH`).

**Machine-local, not in the repo.** The real captures are in `/private/tmp/claude-501/-Users-sf-Developer-RemoteCWKeyer-esp32/bed2bd1e-*/scratchpad/oracle/*.bin` with the downloaded DL4YHF source. **They have already evaporated once** from a machine change within this same session. Not committed: #8 forbids it. If needed, redo them, procedure in `tools/cwnet/README.md`.

## Issues

Open: `#10` TX on MORSE 0x10 · `#11` multi-event RX · `#12` peak-hold filter (with the decision on whether to replicate the bug) · `#13` rigctld PTT · `#14` echo server · `#15` U1 dissector · `#16` devcontainer IDF v5.5.1 · `#17` USB stub · `#19` plan doc review.

Closed as QRM and fixed on the spot: `#18` (IDF path) and `#20` (README).

Blocking, unchanged: `#7` (known sequence, what «identical» means, tolerance) and `#8` (capture provenance). **They still block U5, U6 and committing the fixtures.** Nothing above works around them.

## Awaiting a decision from the user: the two March PRs

Analyzed at the end of the session, **not closed** - they're his PRs and closing them is visible. Both branch from `3c827a1` (March 1); the migration to ESP-IDF v6 is `1b91bec` (May 7): **they're written against v5 and have never seen v6**.

- **[#3](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/3) OTA via Web UI** - git says `MERGEABLE`, but **it does not compile**: its `CMakeLists.txt` has `REQUIRES json`, a component removed in v6 and that CLAUDE.md explicitly forbids; and `api_firmware.c` includes `esp_ota_ops.h`, `esp_app_desc.h`, `esp_partition.h`, `usb_uf2.h` with no matching REQUIRES. Out of bounds twice: `STRATEGY.md` says «no OTA for now» and «WebUI: no investment».
- **[#2](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/2) iambic timeline markers** - `CONFLICTING`. It's not what the title says: it contains the markers, **plus** a captive-portal DNS, **plus** #3's design and plan documents. The central piece widens `sample.flags` from `uint8_t` to `uint16_t` (sample from 6 to 7 bytes): that's a change to the **layout of the architecture's one interface**, on the RT path. The conflicts are in `iambic.c`, where the previous session had fixed two bugs.

**Worth saving, and it's in #2**: the four flags `FLAG_MEM_WINDOW`, `FLAG_SQUEEZE`, `FLAG_MEM_ARMED`, `FLAG_MODE_B_BONUS` are literally `STRATEGY.md`'s K8 metric («which element starts, when it arms the memory, what the squeeze does»). What's out of bounds is the WebUI's Timeline page, not the flags.

**Proposal made to the user, no answer yet**: close both without deleting the branches, and open three issues - FSM instrumentation for the K8 track (with the decision on the 7-byte sample), OTA frozen by boundary, captive-portal DNS parked.

## How to continue

First the decision on the two PRs, which is stalled on him and has the analysis already ready above.

Then, on the unblocked work, two non-exclusive paths: **#14 (echo server)** makes determinism measurable, which had no bench without H6; **#10 (TX on MORSE 0x10)** is the real conformance, the one the project exists for, now demonstrable instead of assumed.

Two loose ends not tracked as issues:

- **The brainstorm on KTD5 is not concluded** - whether a synthetic fixture used as the expected value should break the build, and whether the same holds for the determinism comparison where both ends come from `ours/`.
- **H5 carries a decision**, recorded in #12: replicate the integer-division defect to maximize agreement with the reference, or fix it and produce a better number that won't match.

Useful skills: `ce-work` for the unblocked issues, `ce-brainstorm` if KTD5 is picked back up, `ce-doc-review` for #19.
