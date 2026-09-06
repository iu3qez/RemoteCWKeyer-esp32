# K8 timing tolerance and comparison basis

What counts as agreement between this keyer and the K1EL K8, and why the number
is what it is. `STRATEGY.md` requires this to be written before the test rather
than after, so that a passing result cannot be a tolerance fitted to the outcome.

Companion to issue #32. The measurements behind every figure here are in that
issue's comment thread; the recipe for reproducing them is `components/keyer_iambic/tools/k8/README.md`
(the keyer logic and its oracle live in the Esp32KeyerTest submodule).

---

## 1. What is exact and what is tolerant

The split is not negotiable and comes from `STRATEGY.md`:

**Exact — no tolerance at all.** Which element is sent, and in what order.

- the element a squeeze starts with
- whether a paddle press arms the memory
- which element the memory releases, and when
- what a squeeze release does in Mode A and in Mode B
- the sampling grid: level at instant `k·u`, and at no other instant

A single wrong element is not a small error. It is a different character. These
are pinned by the `test_iambic_k8_*` group in
`components/keyer_iambic/test_host/test_iambic.c`, as
pass/fail with no numeric slack.

**Tolerant — a number.** How long each element lasts.

Everything in the rest of this document is about durations only.

---

## 2. The tolerance

> **One tick of the real-time loop: 1000 µs.**

The loop period is `pdMS_TO_TICKS(1)` with `CONFIG_FREERTOS_HZ=1000`
(`main/rt_task.c:176`), so one tick is 1 ms.

### Why not tighter

Below one tick we can neither hit nor miss, because we cannot represent. The
finite-state machine only advances when the loop wakes it, so every element
boundary lands on a tick by construction. A tolerance of 500 µs would not make
the keyer more accurate; it would make roughly half of all correct results fail
for a reason that has nothing to do with the reference.

### Why not looser

The measured worst case is a tenth of a tick at 25 WPM. A tolerance of one tick
therefore carries about a factor of ten of headroom at the reference speed, which
is enough to absorb the reference's own structure without absorbing a regression.
Two ticks would begin to hide real changes: at 40 WPM two ticks is 6.7 % of a
unit, which is audible.

### What it is anchored to

The tolerance is stated **against the quantum the implementation counts in**,
not against a speed figure. Ours is the 1 ms tick; the K8's is the sidetone
period it counts half-cycles of. A speed figure is a derived, convention-bound
number and is the wrong thing to hang a tolerance on.

---

## 3. The comparison basis

A comparison is only meaningful under all four of these.

### 3.1 Tick-aligned speeds only

Run comparisons only at speeds where `1200000 / wpm` is a whole number of
milliseconds, which is wherever **`wpm` divides 1200**:

| WPM | 5 | 6 | 8 | 10 | 12 | 15 | 16 | 20 | 24 | 25 | 30 | 40 | 48 | 50 | 60 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| unit, ms | 240 | 200 | 150 | 120 | 100 | 80 | 75 | 60 | 50 | 48 | 40 | 30 | 25 | 24 | 20 |

At every other speed our own element durations are rounded **up** to the next
tick, a systematic bias of up to −2.04 % on the effective speed (#39). Comparing
there would measure our defect and attribute it to the reference.

**This is a constraint on the test set, never on the product.** The keyer must
run at whatever speed the operator selects. #39 tracks the fix.

The reference speed is **25 WPM**. 15 WPM is kept as a second point, to show the
conclusion is not an artefact of one speed.

### 3.2 The emulated oscillator is retuned to align the unit

The K8's speed table is `TIMEBASE = floor(1060 / WPM)`, so its labels are not
speeds and no setting of it lands on one of ours. Choose the nearest timebase and
set the emulated clock so the K8's **dit element** equals ours:

| our speed | K8 setting | emulated clock | departure from nominal |
|---|---|---|---|
| 25 WPM | `SPEED_DEFAULT = WPM_26` → `TIMEBASE 40` | 4 024 500 Hz | +0.61 % |
| 15 WPM | `WPM_16` → `TIMEBASE 66` | 3 982 500 Hz | −0.44 % |

This is legitimate, not a thumb on the scale. The K8 runs on the 12C509's
internal RC oscillator with a per-chip factory trim (`morse8.asm:308` loads
`OSCCAL`), so 4 MHz was never a constant of the design — two K8s from the same
batch did not agree on it. What was identical between them is the logic and the
ratios, which is what we compare. Retuning removes a variable the reference
itself did not control.

It is also free: gpsim stimuli and logs are expressed in cycles, so `frequency`
does not alter the simulation. It is a scale factor applied when the trace is
read.

**Align to our nominal unit, never to our effective one.** At a tick-aligned
speed the two are the same number, which is a second reason to insist on 3.1. If
the comparison were ever run at a non-aligned speed, aligning to the effective
unit would tailor the reference around our own rounding and the defect would
become invisible.

### 3.3 The alignment point is the dit element

Align on `mark + space` of the dit, not on the mark alone.

The K8 does not have one unit: it has a slightly longer one for marks and a
shorter one for spaces, because its delay loop shortens the first sidetone
half-period to pay for the time spent in `NSAMPLE`. Aligning on the element
preserves the average rate and splits the residual symmetrically between mark
and space. Aligning on the mark alone would push the whole asymmetry into the
space and double the apparent error there.

### 3.4 One timebase, one scale factor

Each K8 speed setting truncates differently, so each has its own scale factor.
A scale factor measured at one timebase may not be reused at another. Measure it.

---

## 4. What the tolerance has to cover

Measured, not predicted. Full tables in issue #32.

| quantity | 25 WPM | 15 WPM |
|---|---|---|
| worst deviation | +98.4 µs, the dah mark | +256.1 µs, the dah mark |
| as a fraction of one tick | 9.8 % | 25.6 % |
| K8 mark/space asymmetry | 64 cycles, 0.133 % of a unit | 168 cycles, 0.211 % of a unit |
| K8 jitter | none, identical to the cycle | none, identical to the cycle |
| dah/dit ratio on the marks | 3.00006 | 3.00004 |

The asymmetry **shrinks** as speed rises while our tick weighs **more**: 2.08 %
of a unit at 25 WPM against 1.25 % at 15. Both effects push the same way, so the
margin is widest exactly where a keyer is hardest to get right.

---

## 5. What this does not cover

- **Sequence, memory and grid.** Exact, per section 1, and pinned by host tests.
- **Transient behaviour.** Everything here is steady state with one paddle held.
  Squeeze release, memory injection and the first element of a character are
  sequence questions, not duration questions.
- **Latency from paddle to key.** Measured at 56 cycles on the K8, but not part
  of this tolerance: it is a property of the polling loop, and ours is bounded by
  the 100 µs ceiling in `ARCHITECTURE.md`, which is a separate gate.
- **Sidetone and PTT.** Not compared. The K8's sidetone is its own time base, not
  an output we reproduce.
- **Hardware.** Everything here is host-side and emulator-side. What the box does
  with a real paddle at −5 °C is not in scope and cannot be.

---

## 6. How to fail

A timing comparison fails when any measured duration differs from the reference
by more than one tick, at a tick-aligned speed, with the oscillator aligned per
section 3.2 and the scale factor measured at that timebase.

It also fails, regardless of any number, when an element of the wrong type or in
the wrong position appears. That is section 1 and carries no tolerance.
