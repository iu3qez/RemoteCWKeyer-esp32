---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-01T22:50:41Z"
title: "Restarting the project: strategy, CI, and the CWNet test bench"
summary: "A session that gave the project a strategy back, turned the host suite green by finding two real bugs, built CI from scratch, and planned the CWNet test bench up to two blocking issues."
keywords: ["cwnet", "banco-di-prova", "strategia", "ci", "dl4yhf", "issue-bloccanti"]
cwd: "/home/user/RemoteCWKeyer-esp32"
resume_focus: "Resume the evidentiary weight of the fixtures (KTD5) - the user marked it very important at the end of the session - then #7 or U1-U4"
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "3525bf3ca777b861fb4448911a569e6d82b541af"
branch: "claude/remote-environment-setup-vdjx7v"
head: "123405eede29836d774755049016779582e313fe"
---

# Restarting the project: strategy, CI, CWNet test bench

Eight commits on `claude/remote-environment-setup-vdjx7v`, all in [PR #6](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/6). CI green on both matrix variants at the last push.

## Why this session happened

The user opened by saying the project had started with ideas, then the ideas had faded, the foundation was solid but full of inconsistencies because it lacked a strategy. His proposal: tune up strategy, way of working, and CI.

The diagnosis that came out of it, and that holds up everything else: the project has stalled **twice** for the same reason, and it wasn't for lack of will. It lacked a reference to prove things against. The CWNet protocol and the keyer timing both have a real reference, but nobody had ever built the way to verify adherence to it.

## What exists now

**[STRATEGY.md](../../../STRATEGY.md)** - written from scratch with the user, section by section, with pushback. The positioning is the sentence that decides everything: every behaviour that matters is proven against a real reference, not made similar. The protocol's reference is DL4YHF's software; the keyer's is the K1EL K8 firmware (the user corrected his own typo: not K3LR). The Boundaries are sharp and must be read before proposing work: WireGuard is droppable, presets other than the K8 are unvalidated best effort, no OTA for now, WebUI frozen. Test sentence: resist when the only argument is "it's there and it's cheap to add".

**Host suite green**, 189/189, plain and ASan/UBSan. There were 8 red. Two were **real bugs in the firmware**, not stale tests:
- `components/keyer_iambic/src/iambic.c` - the release debounce self-triggered at t=0 because the timestamps started from 0. On the target it blanked the paddles for 5 ms after boot. Documented in `docs/solutions/logic-errors/release-debounce-blanks-first-event.md`.
- Same file - `progress_pct` in Mode B truncated to `uint8_t`: a release past 255% wrapped and fell back under the memory window.

A third (`cwnet_frame.c`, a cast on `payload_len`) only showed up compiling with the sanitizers, and is the reason CI has two variants.

**[.github/workflows/host-tests.yml](../../../.github/workflows/host-tests.yml)** - before, there was no build or test CI at all, only the devcontainer image build.

**[docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md](../../../docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md)** - the bench plan. Read it in full before touching anything CWNet-related. The parts that matter most are the Key Decisions and the table of seven hypotheses under Dependencies.

## The discovery that changes the project

**The reference source is downloadable and current.** `Remote_CW_Keyer_Sources.zip` from qsl.net/dl4yhf, 1.4 MB, files dated up to **October 20, 2025**. The user believed he had no access to the current source. He does. Borland is not needed to read it, only to compile it.

From there, verified by reading `CwNet.h` and `CwStreamEnc.h`:

- `CWNET_CMD_MORSE 0x10`. Our client sends keying on `0x14`/`0x15`, which in the protocol are CI-V and spectrum.
- The MORSE payload is a 7-bit stream: bit 7 = key state, bits 6..0 = wait *before* applying it. We send a 4-byte absolute timestamp. They are two different protocols.
- The 7-bit codec in `components/keyer_cwnet/src/cwnet_timestamp.c` **is already correct** and matches his byte for byte. It has 312 lines of tests. Nobody calls it: TX doesn't use it.
- There is a second end-of-"over" key-up after ~10 dots or 500 ms that we do not emit.
- Latency: the instantaneous formula `t2-t0` is identical to ours. The divergence is **after** - he runs it through an asymmetric peak-hold (rises to the peak, falls by 1/10 of the gap per ping). It's not a measurement, it's a jitter-buffer control parameter. That filter does not exist in our firmware.
- The server without a radio **sends the keying back** to the connected clients. It's the test loop, given to us by the author.

Careful: all of this comes from reading the source, **not from observation on the wire**. The user rightly insisted that these remain hypotheses (H1-H7 in the plan) until a capture confirms them. He suspects that in some spot our code might be more correct than the published source, because at the time they had reconstructed inconsistencies using Wireshark.

## Things the user corrected that need remembering

- **No ESP-IDF in the cloud.** I had started installing it; he stopped me and I removed `/opt/esp`. The firmware is built on his machine.
- **CWNet's purpose is the determinism of what is sent, minus the PTT.** What arrives must be identical to what left, with a tolerance still to be tuned. The PTT gets calibrated after the first rounds.
- **I had said something wrong** and it needs flagging because it's counterintuitive: I claimed the determinism test would pass even with the wrong command byte. False - if the server does not recognize the frames it sends nothing back and the test fails right away. What the loop *doesn't* see is the packing: how many events per frame, where it cuts, the PING cadence. That needs the official capture.
- **Keying by hand is excluded** as the source of the test sequence: below 32 ms the codec resolves to 1 ms, two manual runs do not give the same bytes.
- **Measuring at 293 µs makes no sense**, so the ~10 ms quantization of our turnaround on the PING (from the bg_task's `vTaskDelay`) is not a problem for a number that sizes a buffer.
- **The protocol does not define maximum latencies**, so the PING instrumentation is not a gate.
- **A fixture taken from our own code is deterministic by construction** and proves nothing about conformance. Hence KTD5 in the plan: `reference/` carries the expected values, `ours/` is diagnostic, `synthetic/` only exercises the comparator. U3's harness will be green long before the real captures exist, and that's where synthetic green risks being mistaken for conformance.

## Blockers

Two issues, labelled `blocking`, opened at the user's explicit request:

- **[#7](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/7)** - what the known sequence must contain, what "identical" means, what tolerance. The user said it's the crucial point and deserves a separate brainstorm: the quality of everything else depends on it.
- **[#8](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/8)** - what gets recorded alongside a capture so it can still be adjudicated a year from now.

The user asked to "write in blood that blocking issues block". The rule lives in [CLAUDE.md](../../../CLAUDE.md#a-blocking-issue-blocks), under Critical Constraints: don't plan around it, don't substitute it with assumptions, don't proceed marking the work provisional. Applied to the plan itself too, which is why it stays `artifact_readiness: requirements-only` instead of declaring itself ready.

## Status by plan unit

| Unit | Status |
|---|---|
| U1 fix `register_postdissector` in `tools/wireshark/cwnet.lua` | unblocked, not started |
| U2 pcap → C header extraction chain | unblocked, not started |
| U3 replay harness in `test_host` | unblocked, not started, depends on U2's format |
| U4 PING turnaround instrumentation | unblocked, not started |
| U5 session zero | **blocked** by #7 and #8 |
| U6 the two real comparisons | **blocked** by U5 and #7 |

R13 and R14 in the plan (moving TX to `MORSE 0x10`, the peak-hold filter) are work for the "CWNet client" track, not the bench, and are subordinate to H1/H2 and H5.

## Open work beyond the issues

- **The doc review of the plan's implementation sections was not done.** The five-reviewer review covered the Product Contract, not the Planning Contract or the units. The `ce-plan` skill considers it mandatory.
- **`CLAUDE.md` does not mention `docs/solutions/`.** One line would make the lessons findable by future sessions. I did not add it because `ce-compound` in lightweight mode does not touch instruction files; I proposed it to the user and he has not answered yet.
- **`CONCEPTS.md` does not exist.** Capturing the vocabulary is deferred to a full `ce-compound` run.
- **`.devcontainer/Dockerfile` is still stuck on `ARG DOCKER_TAG=v5.5.1`** with a hardcoded `idf5.5_py3.12_env` path, not updated after the migration to IDF v6. Noted in `.claude/code-quality.md`, not touched: it needs testing on a real `espressif/idf:v6.x` image, which is impossible from here.
- Other candidate lessons for `ce-compound`, not yet written: the truncation of `progress_pct` to uint8, and the dissector as a postdissector (the latter only after U1 fixes it).

## Verification done

`cd test_host && cmake -B build -G Ninja && cmake --build build && ./build/test_runner` → 189/189. Same thing with `-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` → 189/189, zero reports from the sanitizers. CI green on both matrix entries.

`tshark`, `tcpdump` and `editcap` **are not installed** in this container, so the assumption about `tshark -q -z follow,tcp,raw` in the plan has not been empirically tested. It's declared as an assumption to verify on first use.

## How to continue

**To resume first, by the user's explicit wish:** the evidentiary weight of the fixtures, KTD5 in the plan. Closing the session he said "tomorrow we come back to this point, very important". The core of it: a fixture generated from our own code comes back green by construction, and U3's harness will be green long before a real capture exists. The open question is not whether the distinction is needed - that's settled - but how far it should be carried: how the outcome declares the category, whether a synthetic fixture should be able to fail the build when someone uses it as the expected value, and whether the same logic applies to the determinism comparison, where both ends come from `ours/`.

Then, two paths, not alternatives to each other but in different orders:

1. **Unblock #7 with the dedicated brainstorm**, which the user has already said he wants kept separate. It's the path that makes everything else useful, because without it session zero cannot happen.
2. **Start from U1-U4**, which do not depend on any issue and build the infrastructure that will consume the fixtures once they arrive. U3 is developed against a synthetic fixture precisely so as not to wait.

Useful skills: `ce-brainstorm` for #7, `ce-work` for the unblocked units (but only after noting that the plan is `requirements-only` on purpose), `ce-doc-review` for the debt flagged above.
