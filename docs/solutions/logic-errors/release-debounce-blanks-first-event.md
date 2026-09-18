---
module: keyer_iambic
date: 2026-09-01
problem_type: logic_error
component: real_time_path
severity: medium
symptoms:
  - "Five iambic host tests fail: the FSM stays in IAMBIC_STATE_IDLE on the first tick with a paddle pressed"
  - "TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state) reports Expected 1 Was 0"
  - "On hardware, both paddles are ignored for the first 5 ms after boot or after iambic_reset()"
root_cause: uninitialized_sentinel
resolution_type: code_fix
related_components:
  - keyer_decoder
  - keyer_webui
tags:
  - debounce
  - timestamp
  - initialization
  - real-time
  - iambic
---

# The release debounce that swallows the first event

## Problem

Five iambic tests failed the same way: with a paddle pressed on the first tick, the state
machine stayed in `IAMBIC_STATE_IDLE` instead of starting the element.

```
test_main.c:36:test_iambic_dit:FAIL: Expected 1 Was 0
test_main.c:61:test_iambic_dah:FAIL: Expected 2 Was 0
test_main.c:87:test_iambic_mode_a_squeeze:FAIL: Expected TRUE Was FALSE
```

They looked like tests gone stale against an evolved implementation. They were not.

## Cause

`update_gpio()` in `components/keyer_iambic/src/iambic.c` applies blanking after release, to
suppress contact bounce on opening:

```c
bool dit_in_blanking = (now_us - proc->dit_release_time_us) < IAMBIC_DEBOUNCE_RELEASE_US;
bool new_dit_pressed = raw_dit && !dit_in_blanking;
```

`iambic_init()` and `iambic_reset()` initialized `dit_release_time_us` and
`dah_release_time_us` to `0`. With `now_us = 0` on the first tick, the expression becomes
`0 - 0 < 5000`, which is true: the blanking window turns out active **before any release has
ever happened**, and the paddle is ignored.

The zero is ambiguous: it means both "instant zero" and "never happened", and the comparison
cannot tell them apart.

On the target the effect is minor but real - the paddles stay deaf for 5 ms after boot - while
on the tests, which deliberately start from `now_us = 0`, it is fatal.

## Solution

Timestamps meaning "never happened" start a full window in the past, so the comparison is false
by construction until a real release updates them:

```c
/* No release has happened yet: park the timestamps one blanking period
 * in the past so the release debounce does not swallow a press at t=0. */
proc->dit_release_time_us = -IAMBIC_DEBOUNCE_RELEASE_US;
proc->dah_release_time_us = -IAMBIC_DEBOUNCE_RELEASE_US;
```

Applied both in `iambic_init()` and in `iambic_reset()`. The five tests pass without touching
them.

## Two idioms, and when to choose which

The same pattern - a guard computed on a timestamp that might never have been written -
appears twice more in the repo, and both were already correct with a different idiom, the
explicit sentinel:

```c
/* components/keyer_webui/src/api_config.c:151 */
if (last_save_us > 0 && (now_us - last_save_us) < (int64_t)10 * 1000000) {
```

```c
/* components/keyer_decoder/src/decoder.c:158 */
if (s_state != DECODER_STATE_RECEIVING || s_last_event_wall_us == 0) {
    return;
}
```

The sentinel is more explicit and should be preferred where readability matters most. The value
parked in the past avoids a branch and keeps the expression uniform, which makes sense in
`update_gpio()`, which sits on the hard-RT path of Core 0 and computes both windows on every
tick with no branching.

What does not work well is the third way: initializing to zero and hoping `now_us` is large
enough. It works until someone calls the function with a small time - a test, a reset, the
first tick after boot.

## Prevention

When you write a guard of the form `(now - last_X) < WINDOW`, ask what `last_X` is before X has
ever happened. If the answer is zero, the guard is active at startup and is suppressing the very
event it is supposed to protect. Choose the sentinel or the value parked in the past, never the
implicit zero.

And a corollary of method: five tests failing the same way on a mature component look a lot like
aged tests. Before rewriting them, it is worth reading what they actually assert - here the
assertion that failed was on the line before the one that looked like it.
