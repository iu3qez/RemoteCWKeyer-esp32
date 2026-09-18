---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-07T18:21:04Z"
title: "The keyer logic lives in Esp32KeyerTest; #44 is four mechanisms and a corpus that doesn't exist"
summary: "Session that traced the divergent case of #44, had half the diagnosis disproven by a review, changed the strategy (K8 = feeling, black-box corpus) and moved the FSM into its own repo, consumed as a submodule."
keywords: ["esp32keyertest", "submodule", "k8", "issue-44", "corpus", "black-box", "strategy", "feeling", "deploy-key", "axes"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
resume_focus: "On the Esp32KeyerTest repo: the axis-based restructuring of issue #1 (no behaviour change) and, on the main repo, the lever capture (#54) that unblocks the corpus (#8)."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "3abb071"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
---

# The keyer logic lives in Esp32KeyerTest

Session of September 6-7, 2026, resumed from `2026-09-06_1245_k8-sampled-mode-diverge.md`.
It set out to fix #44 and ended up changing where and how that work gets done.
Whoever picks this up needs to know this first: **the iambic FSM is no longer in this repo.**

## Where things are now

- **[iu3qez/Esp32KeyerTest](https://github.com/iu3qez/Esp32KeyerTest)** (private; local
  clone `~/Developer/Esp32KeyerTest`, machine-local): the root is the ESP-IDF component.
  Inside: `include/`, `src/`, `test_host/` (26 tests, `interface/sample.h` is a frozen
  copy), `tools/k8/` (oracle + bench as a skeleton + `trace_decode.py`),
  its own `STRATEGY.md` and `CLAUDE.md`, the three issue templates. Eight open issues.
- **This repo** consumes it as a submodule in `components/keyer_iambic`, pinned to
  `05d183f` (the keyer repo is three commits ahead, all documentation: the bump isn't
  urgent). PR **#53** merged in `8dea26f`, CI green on both jobs. The host suite here is
  176 tests; 176 + 26 = the 202 from before.
- **CI and the private submodule**: `GITHUB_TOKEN` doesn't grant access to it. The
  workflows do a normal checkout then `insteadOf` towards ssh and `submodule update` with
  a read-only deploy key (id `162463041` on the keyer repo; secret
  `ESP32KEYERTEST_DEPLOY_KEY` here). The private key doesn't exist on disk. **Don't
  "simplify" into `ssh-key:` of `actions/checkout`**: with a key set it doesn't rewrite
  the submodule URLs (`git-auth-helper.ts:77`, read).

## The maintainer's decisions, in order

All from the maintainer, explicit, in chat; written into `STRATEGY.md` here (commit
`b883403`) and into `Esp32KeyerTest/STRATEGY.md`:

1. **The K8 is the reference for feeling, not for the implementation.** Feeling = human
   input → K8 output, reproducible, **up to 40 WPM**. The limits of a PIC12 (a 14 µs
   polling window) aren't ours. Above 40 WPM there's no reference, no metric.
2. **The source names, the black box judges.** The K8 source picks the configuration
   axes and controls coverage; the expected value comes only from the oracle run on a
   **corpus of real keying** (errors included); a divergence counts only if it's stable
   across phase. Tolerance lives in the comparator, never in the model.
3. **Universal logic by axes**: a behaviour enters as a value on an axis, never as an
   `if` on a keyer model; the K8 is a preset. An axis opens when a real reference sits
   elsewhere; unproven values don't get populated.
4. Two tracks in the keyer repo: **Bench** (wins) and **Axis-based FSM**.
5. Without a corpus you **restructure** (behaviour unchanged, tests green) but don't
   change behaviour; **coverage is a gate**; **the main repo does the capture**.
6. New private repo, consumed as a submodule (not a branch, not a vendored copy).

**My** choices, not the maintainer's: repo root = component; suite split 176/26;
`FINDINGS.md` not migrated (numbers superseded); deploy key instead of PAT; #32 left
here and closed by the sweep; the wording of the issue bodies.

## What the trace established, and what the review disproved

Everything is in `Esp32KeyerTest/tools/k8/bench/README.md` and in the body of
**Esp32KeyerTest#1** (formerly #44, rewritten). In short: four mechanisms at the element
boundary (same-type re-arm after `BCF` at `morse8.asm:374` via `AUTOSP :536`; `INLAST`
tiebreak in `CHK_SINGLE`; `squeeze_seen` armed at mid-element with unreachable
cancellation in SAMPLED; decision order with `TOGGLE` before memory). **Only the first
has stable proof across phase** (8 cases). The "124 out of 436" is withdrawn: the oracle
only runs in Mode B and the sweep was also comparing our Mode A; 32 malformed stimuli;
26 of the 50 real divergences shift with 4.7 µs. The representative case from the
previous handoff sits 2 cycles inside a window of 56.

The adversarial review (agent on `opus`, mandated to falsify) is the pattern that
worked: two of my six claims were disproven. Repeat it.

## Tracker status

Main repo: #32 **closed** on evidence (sweep of 2026-09-07); #26 `narrowed` - the
maintainer narrowed the LEDs to a binary indication in a comment, the body isn't
updated; #54 queued (capture). The other open ones are CWNet/USB/console, unchanged.

Esp32KeyerTest: #1 (formerly #44), #2 (formerly #39), #3/#4 parked (formerly #37/#35), #5
host model as gpsim cache, #6 oracle in Mode A, #7 `SLEEP` patch to verify against the
source (**not** verified by me: the review says the wake from `SLEEP` goes through
`CLRF PROCLAT` `:774`), #8 corpus + comparator. Chain: **#54 → #8 → #1**. Nothing
`blocking`.

## Light debt, in `.claude/code-quality.md`

`treecode` blocks in `test_host/CLAUDE.md` and `keyer_core/CLAUDE.md` need resyncing
with `map-tree`; remote branch `k8-differential-bench` to delete once it's not needed.
The universality principle isn't in `ARCHITECTURE.md` here: it lives in
`Esp32KeyerTest/CLAUDE.md` and `STRATEGY.md`, which is where the FSM lives now - whether
it's needed here too is the maintainer's call.

## Machine-local state, fragile

- `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/k8/`: original `morse8.asm` (sha256
  `432df077…`, don't touch), patched copy, `morse8_tb40_nosleep.hex` (`e1764e09…`),
  gpsim log. The hex reassembles from the README: verified identical.
- Local branches `wip-k8-decision-order-attempt` (abandoned attempt, diagnosis
  superseded but worth keeping) and `k8-differential-bench`.
- The session scratchpad evaporates: what mattered (decoder, gpsim script) is in the
  keyer repo.

## Verification done

CI green on `8dea26f` (host-tests plain and ASan/UBSan, firmware-build esp32s3) and on
the keyer repo at the first push. Locally 176/176 + 26/26 in both variants, `sample.h`
identical. `firmware-build` **not** runnable on this machine: no ESP-IDF.

## Continuation

Only one path forward, across two repos:

- **Esp32KeyerTest #1, first half**: restructure `iambic.c` by axes without changing
  behaviour, suite green - the only FSM work authorised without a corpus. `ce-plan`
  over there, with the adversarial review before writing.
- **Main repo #54**: the capture on the box (needs hardware). Unblocks #8, which
  unblocks the second half of #1.
- Independent and small: #6 (Mode A in the oracle), #7 (verification of the `SLEEP`
  patch), #5.
