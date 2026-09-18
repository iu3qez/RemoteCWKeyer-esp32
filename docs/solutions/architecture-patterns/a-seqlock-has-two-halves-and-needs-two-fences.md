---
title: "A seqlock has two halves, and needs two fences"
date: 2026-09-08
category: architecture-patterns
module: keyer_core
problem_type: architecture_pattern
component: real_time_path
severity: high
applies_when:
  - "A producer and one or more consumers share a slot via an index published with an atomic store/load, like keying_stream_t, and the consumer validates the copy by rereading the index"
  - "A configuration snapshot is protected by a generation counter reread after copying the fields"
  - "A stress test passes locally and under sanitizer but a CI leg on arm64 intermittently fails it"
symptoms:
  - "3-6 torn samples accepted by test_stream_two_threads_never_accept_a_stale_or_torn_sample under ASan on Apple M1 (arm64), with an acquire load in place of the reader fence"
  - "1 and 80 torn samples on the arm64 plain CI leg (ubuntu-24.04-arm) without the writer's release fence, never reproduced on M1 or x86-64"
  - "The defect is invisible on x86-64 (TSO) and on some toolchain/sanitizer combinations, so a green local run does not rule it out"
root_cause: concurrency
resolution_type: code_fix
related_components:
  - main
  - test_host
tags:
  - seqlock
  - memory-model
  - fences
  - acquire-release
  - ring-buffer
  - arm64
  - stdatomic
  - real-time
---

# A seqlock has two halves, and needs two fences

## Context

`keying_stream_t` is the only interface between the RT producer on Core 0 and the consumers on
Core 1: a power-of-two ring, a single `write_idx` index, no lock. The shape is that of a
degenerate seqlock, where the sequence number is the write index itself: the consumer copies the
slot and then rereads the index to decide whether the copy is still valid.

The code, before #57, looked correct and passed the entire host suite. `stream_write_slot()`
took the index with `atomic_fetch_add_explicit(&write_idx, 1, memory_order_acq_rel)` and
stored the sample *afterwards*, so a consumer that loaded `write_idx` in between would see a lag
of 1 and read a slot that was half old and half new; `stream_read()` also accepted
`behind == capacity`, i.e. exactly the slot the producer is about to write (issue #57,
Evidence section). The defect surfaced from an adversarial review, not from a test.

The obvious fix, store-then-publish with a release store and `behind >= capacity` as the overrun
condition, was not enough. The first version reread the index with an acquire load (session
history): the stress test still accepted 3-6 torn samples under ASan on Apple M1, which is
arm64. With that half closed by the fence, the arm64 `plain` CI leg accepted 1 and 80 torn
samples across two runs, while x86-64 and M1 showed nothing (PR #70, «What changes» section).
That failure never reproduced on the development machine: the arm64 runner was the judge, not
the laptop (session history). The `docs/solutions/` corpus had nothing on the memory model: this
entry is that gap.

## Guidance

**The two C11 guarantees, stated precisely.**

- An `atomic_load_explicit(..., memory_order_acquire)` orders the loads and stores that come
  **after** it. It does not order those **before**: on a weakly-ordered core, an earlier load
  can complete after the acquire load.
- An `atomic_store_explicit(..., memory_order_release)` orders the loads and stores that come
  **before** it. It does not order those **after**: subsequent stores can become visible before
  the release store.

From this follow the two halves of the seqlock, and neither is covered by the ordering tag on
the atomic operation: two `atomic_thread_fence()` calls are needed.

**Reader half.** Revalidation only makes sense if the payload copy has already finished by the
time the index reread happens. The *initial* acquire load does not serve this purpose - it
orders what comes after. Between the copy and the reread you need
`atomic_thread_fence(memory_order_acquire)`, and the reread can then be `relaxed`. In
`components/keyer_core/src/stream.c:144-156`:

```c
    stream_sample_t copy = stream->buffer[idx & stream->mask];

    atomic_thread_fence(memory_order_acquire);
    write = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    if (overrun_at(write, idx, stream->capacity)) {
        return false;
    }

    *out = copy;
```

The discard predicate is `overrun_at()`, `components/keyer_core/src/stream.c:29-31`:
`write - idx >= capacity` in wrapping arithmetic. The `>=` is not a detail, it is the second bug
in #57: the slot `capacity` behind the write position is the producer's *next* one, hence
already lost. `consumer_resync()` consistently lands `capacity - 1` behind
(`components/keyer_core/src/stream.c:236-249`).

**Writer half.** `write_idx == idx` is the announcement that slot `idx` is about to be written,
and that announcement is the *preceding* release store. A release store does not order the
stores that follow it, so the slot's bytes can become visible before the announcement, and the
reader's revalidation would pass on a torn copy. You need
`atomic_thread_fence(memory_order_release)` between the previous publish and the slot store.
In `components/keyer_core/src/stream.c:72-82`:

```c
    size_t idx = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    size_t slot_idx = idx & stream->mask;

    atomic_thread_fence(memory_order_release);
    stream->buffer[slot_idx] = sample;

    atomic_store_explicit(&stream->write_idx, idx + 1, memory_order_release);
```

The relaxed load of `write_idx` is legitimate because there is only one producer: nothing else
moves that index. This is the decision the maintainer made in #57, recorded as Amendment 003 in
`ARCHITECTURE.md:563-567`, with the rules rewritten in `ARCHITECTURE.md:81` (2.1.4, a single
producer), `:119` (3.1.2, release fence, store, publish) and `:121` (3.1.3, reread behind an
acquire fence). Store-then-publish with a single counter is sound *because* there is one
producer; with multiple producers you would need a per-slot sequence number, an alternative
discarded in #70.

**The same idiom applies to any snapshot protected by a generation counter**, not just a ring.
`main/rt_task.c:189-212` rereads the configuration with the same scheme: copy the fields,
acquire fence, relaxed reread of `g_config.generation`:

```c
            atomic_thread_fence(memory_order_acquire);
            uint16_t gen_after = atomic_load_explicit(&g_config.generation, memory_order_relaxed);
            if (gen_after != current_gen) {
                continue;  /* Torn read - retry next tick */
            }
```

The comment above these lines also states what the guard does **not** prove, and that is the
part that gets forgotten: every setter bumps the generation *after* its own store, so a store in
flight is invisible to the reread, and a mixed set of fields can be applied for one tick;
`last_config_gen` keeps the pre-read value, so the bump that made the set mixed forces a reload
on the next idle tick. Each field is its own atomic, so no individual value is ever torn. The
first draft of the comment sold the guard as protecting the whole set; the #71 review corrected
it (session history). A guard that truly protected the *set* would need a pre-store bump on the
writer, i.e. a design change to `keyer_config` that nobody has asked for.

**The test must make the tear observable.** A sample that carries no information about itself
cannot distinguish a coherent copy from a mixed one: here `sample_for_index()`
(`test_host/test_stream.c:128-134`) derives three distinct fields from the index, and
`sample_is_index()` checks all three, so a partial or stale copy shows up. The pin is
`test_stream_two_threads_never_accept_a_stale_or_torn_sample`, `test_host/test_stream.c:194`:
a producer on a thread that never waits, 400k samples on a 64-slot ring, every accepted sample
must be the one its index says. Two non-decorative details: the stress loop has a bound
(`idle_turns > 100000000u`, line 229) because a regression must fail rather than hang the
runner, and there is a floor on the number of accepted samples (line 239) that fails a run in
which the two threads effectively serialized, i.e. one in which the test tested nothing.

## Why it matters

`ARCHITECTURE.md:332`: «Corrupted CW timing is worse than silence». A torn sample is exactly
that. A tear on a silence marker delivers a made-up count to whoever is counting ticks, a tear
on a sample delivers an edge that never existed: the decoder gets it wrong, remote keying emits
timing nobody produced, and there is no FAULT because from the consumer's point of view the data
is formally valid. It is the worst possible way to break: silent and plausible.

And the failure is platform-dependent. x86-64 is TSO and cannot fail on a missing fence of this
kind; on M1 the first tear showed up only under ASan, whose instrumentation slows the consumer
down enough to open the window, and the second never showed up at all. The only leg that failed
the half-fixed version was `ubuntu-24.04-arm` without a sanitizer (PR #70). So: **a
memory-ordering fix is not proven by a green run on the development machine.** Doubling the jobs
in `.github/workflows/host-tests.yml` (an os × name matrix, `ubuntu-latest` and
`ubuntu-24.04-arm`, `plain` and `asan-ubsan`) paid for itself the first time it was used. One
matrix detail that nearly undid it: with only `os` as an axis, the second `include` overwrote
the first and the `plain` legs silently disappeared; `name` is an explicit axis for this reason
(session history).

## When to apply it

- Any lock-free SPSC/SPMC structure in this repository where a payload is validated by an index
  or generation read **after** the copy. Today: `stream_read()`, and anyone adding a second ring
  with the same shape.
- Any configuration snapshot protected by a generation counter, as in `rt_task.c`. Write in the
  comment both what the guard proves and what it does not prove.
- Any time you are tempted to replace a fence with an ordering tag on a load or store: ask which
  side of the fence the access to be ordered is on. If it is on the wrong side, the tag does
  nothing.
- As an evidence requirement: a PR touching memory ordering is green on the arm64 leg, `plain`
  and `asan-ubsan`, or it is not proven.

## Examples

The numbers observed, from PR #70 and the session handoff:

| missing half | platform | torn samples accepted |
|---|---|---|
| reader's acquire fence (acquire load in its place) | Apple M1 (arm64), ASan | 3-6 |
| writer's release fence | arm64 CI, `plain` leg | 1 and 80 across two runs |
| writer's release fence | x86-64 and Apple M1 | 0, never reproduced |
| both (pre-#57 state, `fetch_add` before the store) | host, both variants | 16327 |

The host suite went from 215 to 218 tests with #70, unchanged at 218 with #71 (`main/` does not
compile on the host: that call site is covered only by the firmware build).

## Related

- `components/keyer_core/src/stream.c`: the two fences, `overrun_at()`, `consumer_resync()`
- `main/rt_task.c:189-212`: the same acquire fence for the configuration snapshot
- `test_host/test_stream.c:194`: the two-thread pin; `:142` and `:159` for overrun and resync
- `ARCHITECTURE.md`: rules 2.1.4, 3.1.2, 3.1.3, 3.2 and Amendment 003 (line 563)
- `components/keyer_core/CLAUDE.md`: the short version of the two rules, and the warning
  «publish-after-store, not before» for anyone touching `stream.c`
- `CODING_STYLE.md`, «Memory ordering guide»: says acquire for the consumer and release for the
  producer, without this caveat; this entry is the case where that guide is not enough
- Issue #57 and #69, PR #70 and #71 (merged on 2026-09-08)
- Boehm, *Can seqlocks get along with programming language memory models?* (2012), cited in #69
