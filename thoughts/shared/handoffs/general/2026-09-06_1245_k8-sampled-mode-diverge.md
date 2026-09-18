---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-06T12:45:54Z"
title: "SQUEEZE_MODE_SAMPLED diverges from the K8, and three correction attempts failed"
summary: "The differential bench shows 124 divergences out of 436 cases; the diagnosis is written, the probable cause is known, but three implementations in a row made the count worse and were abandoned."
keywords: ["k8", "k1el", "squeeze-mode-sampled", "issue-44", "oracolo-differenziale", "gpsim", "iambic", "inlast"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
resume_focus: "Fix SQUEEZE_MODE_SAMPLED against the K8, tracing one divergent case end to end before touching a line."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "docs-compound-oracle-gradient"
head: "a8ccb10"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
---

# The sampled mode diverges from the K8

Session of September 6, 2026, resumed from the handoff `2026-09-06_0112_k8-oracolo-eseguibile.md`.
It produced seven merged PRs and one open defect that was not resolved. This
document exists mostly for that defect.

## Goal of the next session

Issue **#44**: `SQUEEZE_MODE_SAMPLED` loses elements the K8 sends. The defect is
in code merged today with #38, it's not inherited debt.

Maintainer's decision, explicit: **we follow the K1EL standard**, not
an approximation. And: we stay on the K8 as the sole reference, «one thing,
done well» (Keyrama is parked in #37).

## The method, which is the part that matters

This session's error was not one of knowledge but of method, and it's
documented in
[docs/solutions/architecture-patterns/differential-oracle-count-is-not-a-gradient.md](../../../../docs/solutions/architecture-patterns/differential-oracle-count-is-not-a-gradient.md)
which is on `main`.

**Read it before touching `iambic.c`.** In short: the divergence count is a
gate, not a gradient. Trace **one** divergent case end to end on both sides,
ours tick by tick and the reference instruction by instruction in gpsim,
before changing a line. Always group divergences by shape, never just read
the total.

## What we know about the cause

To read, in this order:

- **#32**, comments from the bottom up: the verified spec of the K8
  sampling, the latch rule, and the maintainer's decisions.
- **#44**: the defect's diagnosis and the sweep's tables.
- `tools/k8/bench/FINDINGS.md`: the same tables in stable form.

The core of it. The K8 samples the paddle level on a one-unit grid and
**records** what it finds in `PROCLAT`. At the last instant of an element it
does three things in order: samples, clears the bit of the type just sent
(`morse8.asm:374` for a dit, `:409` for a dah), samples again in `AUTOSP`
(`:536`). The second sample rearms the same type if the contact is still
closed. Our implementation only does the clearing and otherwise reads the
**live** state of the paddles at the next decision. A fact once recorded
survives the finger lifting, a question asked afterward doesn't.

An agent on opus established, by reading the source, that `NSAMPLE` on the
paddle bits only does `BSF` and never `BCF`, so **it's idempotent**: the
`AUTOSP` and `SERVLOOP` samples are not two distinct observations. It follows
that modelling the boundary as a single sample is correct, and that the
question «one or three samples» never had two true alternatives. It also
warned that, once both bits can be on together, the tiebreak on `INLAST`
becomes load-bearing and we don't have it. `INLAST` is therefore inside #44,
not later work.

Its falsification test, not yet run: if there's a case where the divergence
follows the **phase inside the 30 µs window**, with a ten-microsecond shift
of an edge changing the emitted element, then the idempotency reading is
wrong and the assembly wins.

## What was tried and failed

Local branch **`wip-k8-decision-order-attempt`** (`cba48c4`), not pushed,
**do not merge**. The commit message is written specifically for this
handoff and should be read: it separates what looks correct from what
doesn't.

| step | divergences out of 436 |
|---|---|
| state on `main` | 124 |
| after reordering the decisions | 225 |
| after the fix on `BOTH_ON` | 145 |

In the end 141 of the 145 were a single shape, «we lose the final element»,
which is not the original defect. The residual defect **was not
identified**: the suspicion is the handling of `BOTH_ON` in mode A, which
zeroes a latch the K8 seems to keep, but it's a suspicion, not a
conclusion.

These look correct, and stand as a starting point: the sampler rewritten on
the `NSAMPLE` model, sampling at rest (because `SERVLOOP` samples freely
when no element is in flight, `morse8.asm:776`), the `in_last_dit` field
kept distinct from `last_element`, and having limited the K8 decision order
to `SQUEEZE_MODE_SAMPLED` alone, leaving the two window modes untouched.

## The bench

Branch **`k8-differential-bench`** (`fae2f57`), pushed, **no PR**. Contains
`tools/k8/bench/` with `k8seq.py`, `sweep.py`, `ours_seq.c` and `FINDINGS.md`.
A run costs about a tenth of a second, so a sweep of 436 cases is practical.

It stands on its own, independent of the fix: it's what found the defect.
Opening a PR is still a choice to be made.

## Machine-local state, fragile

Absolute paths, outside the repository and gitignored. They survive a
restart but are not in git.

- `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/k8/` - the original
  `morse8.asm` (**do not touch**, sha256 `432df077a197…`), the patched copy
  `morse8_gpasm_tb40_nosleep.asm`, its `morse8_tb40_nosleep.hex`
  (sha256 `e1764e099f7c…`), copies of `k8seq.py`, `sweep.py`, `ours_seq.c`,
  the timing-comparison result in `k8-timing-comparison-RESULT.md`, and in
  `dj5il/` the extraction of the DJ5IL articles with the PDFs.
- The session scratchpad **evaporates**: everything that was needed has been
  copied above.

The three patches needed to run the oracle are documented in
`tools/k8/README.md`, so the hex can be rebuilt even if it's lost. The third
one, replacing `SLEEP` with `GOTO SERVLOOP`, is verified: patched and
unpatched produce identical keying, cycle for cycle.

## Verification done

Host suite **202/202 green**, plain and ASan/UBSan variants, on `main` and
on every branch delivered. The timing comparison against the K8 was run at
25 WPM and at 15 WPM: every deviation is within one tick of our loop, the
worst is a tenth of a tick at 25 WPM. The tolerance that came out of it is
written in `docs/k8-timing-tolerance.md`, merged.

## The rest of the session, already closed

Merged: #38 (the sampled mode, which is what #44 fixes), #41, #42, #43,
#45, #47, #50, #51. Open: **#52** (the method document), plus #2 and #3 from
March, parked and untouched.

Issues opened along the way and not yet addressed: **#46** (the line editor
has no host coverage), **#48** (`usb_cdc_connected()` reports initialization,
not host presence, and the wait loop in `main.c` doesn't wait), **#39**
(rounding to the tick, up to 2% slower, downgraded from blocking once it was
decided to test only at aligned speeds).

No `blocking` issue open.

## Plausible continuations

The only natural continuation is #44, and the first step is not writing
code.

The case to trace must be chosen **from the most numerous shape**, not at
random: the understanding that comes out of it covers most of the
divergences instead of explaining only one. At the point it was abandoned
that shape was «we lose an element», 105 cases out of 124, and the
representative already isolated and reproduced in
`tools/k8/bench/FINDINGS.md` is this one, at 25 WPM where one unit is
48 ms:

    dit pressed at 0
    dah pressed at 0.25u
    dit released at 2.0u
    both released at 2.5u

The K8 sends `..-`, we send `.-`.

Trace it on both sides, ours tick by tick and the reference instruction by
instruction in gpsim, until you know **which line** is wrong. Only then
touch `iambic.c`, and only then rerun the sweep to confirm, never to
decide.

The maintainer indicated to do this in a new session with a clean context,
alongside an adversarial review agent: that pattern worked demonstrably
today, because the agent on opus rejected the wrong framing it had been
given.

Note for whoever falls back on a subagent: **in this build it is not
possible to send a follow-up message to a subagent already running**. A
long, exploratory task delegated in the background cannot be steered
partway through.
