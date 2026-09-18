---
title: "A differential oracle's count is not a gradient"
date: 2026-09-06
category: architecture-patterns
module: keyer_iambic
problem_type: workflow_issue
component: testing_framework
severity: high
applies_when:
  - "A differential oracle returns a divergence count instead of a binary verdict"
  - "You are about to change a state machine and remeasure to decide whether to keep the change"
  - "The sweep runs over a sampled family of stimuli, not the exhaustive domain"
tags:
  - differential-testing
  - oracle
  - method
  - k8
  - keyer-iambic
  - debugging
---

# A differential oracle's count is not a gradient

## Context

The keyer's differential bench (`tools/k8/bench/`) sends the same paddle stimulus to the
K1EL K8 emulated in gpsim and to our FSM, then compares the element sequences. Across 436
cases at 25 WPM it started at **124 divergences**, with a correct diagnosis of the cause
(issue #44, today iu3qez/Esp32KeyerTest#1) and a reasoned recommendation on how to fix it.

The implementation was measured after every step:

| step | divergences |
|---|---|
| starting state | 124 |
| after reordering the decisions | 225 |
| after the fix on `BOTH_ON` | 145 |

Three attempts, all worse than the starting point, and the work was abandoned on a `wip-`
branch without ever understanding which line was wrong.

The bench was correct. The diagnosis was largely correct. **What was wrong was the use of
the number.**

## Guidance

**1. The count answers "is it better", never "why".** It is a gate, not a gradient. It is
there to decide whether a fix is finished, not to choose which fix to make.

**2. Before changing a line, trace ONE divergent case end to end on both sides.** Ours tick
by tick, the reference instruction by instruction in the emulator. Pick the case from the
most numerous class, so the understanding it yields covers most of the divergences.

**3. A change is justified by the trace, not by the delta.** If you cannot say in advance
which way the count will move and by roughly how much, you have not yet understood the
change you are about to make.

**4. Always look at the composition, not just the total.** Group the divergences by
`(reference, ours)` pair and count each shape. The total hides exactly the information
you need.

**5. If you abandon it, keep the attempt with its numbers and the diagnosis of the
method**, not just the code. A `wip-` branch with an honest commit message costs nothing
and stops the next attempt from retracing the same path.

## Why it matters

The twin pattern, `reference-source-as-differential-oracle.md`, describes an **exhaustive**
comparison that returns a binary answer: zero discrepancies over the whole domain, or not.
That shape does not lend itself to this misuse.

A sweep over a sampled family of stimuli instead returns a **count**, and a count looks
like a gradient. It is not, for three reasons: the cases are not independent, a single
change can fix one class and break two, and the total does not say which.

The concrete case is instructive. Going from 124 to 145 looked "almost back to the
starting point". But the composition had changed entirely: 141 of the 145 were now a
single shape, "we lose the final element", which was not the original defect. The number
said "a bit worse". The composition said "a different bug".

## When to apply it

Any time the oracle returns a count instead of a verdict. And in particular the moment
you notice you are running the sweep to decide whether to keep a change: that is the
signal you are using the number as a gradient.

## Examples

The grouping that would have caught the error on the first pass instead of the third:

```python
import collections
c = collections.Counter()
for key, ev in cases():
    k = k8seq.run(ev)          # reference
    o = ours(ev)               # ours
    if k != o:
        c[(k, o)] += 1
for (k, o), n in c.most_common(8):
    print(f"{n:4d}x  ref={k!r:10s} ours={o!r:10s}")
```

Output on the third attempt, which makes the change in the defect's nature obvious:

```
  65x  ref='.-.'      ours='.-'
  32x  ref='..-'      ours='.-'
  20x  ref='....'     ours='...'
  17x  ref='-.-'      ours='-.'
```

Four shapes, all "missing the last element". A total of 145 does not say that.

## Related

- `docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md`
  - the pattern this documents a way of failing at
- `components/keyer_iambic/tools/k8/bench/` - the bench's skeleton, in the
  Esp32KeyerTest submodule; its README explains why the synthetic sweep is superseded
- `components/keyer_iambic/tools/k8/README.md` - how to run the K8 oracle in gpsim
- iu3qez/Esp32KeyerTest#1 (the defect, transferred from #44), #32 (the reference's
  verified specification)
- Branch `wip-k8-decision-order-attempt` - the abandoned attempt, not to be merged
