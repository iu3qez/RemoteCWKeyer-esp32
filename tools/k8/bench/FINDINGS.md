# Differential sweep, K8 emulator against our FSM

Run: 25 WPM, `TIMEBASE 40`, emulated clock 4 024 500 Hz, Mode A and Mode B.
436 stimulus cases. **124 diverge.**

| family | mode A | mode B |
|---|---|---|
| A — dah first, dit joins, staggered release | 20/88 | 5/88 |
| B — dit first, dah joins, staggered release | 43/88 | 24/88 |
| C — one paddle held, opposite tapped at varying phase and width | 11/42 | 21/42 |

## The dominant class: we drop an element the K8 sends

| count | K8 | ours | |
|---|---|---|---|
| 32 | `..-` | `.-` | we lose one |
| 26 | `.-.` | `.-` | we lose one |
| 16 | `-.-` | `-.` | we lose one |
| 11 | `--` | `-.-` | we add one |
| 10 | `....` | `...` | we lose one |
| 8 | `.-.-` | `.-.` | we lose one |
| 5 | `....` | `..-.` | same length, wrong type |
| 4 | `-.-.` | `-..` | we lose one |
| 4 | `-.-.` | `-.-` | we lose one |

105 of 124 are "we lose one element". Not a harness artefact: the divergence is
identical with a tail of 8 units and of 30 units.

## Diagnosis

Issue #32's verified reading of the K8 says the same-type latch is cleared
**after the last in-element sample**, and that a same-type press *"survives only
if the paddle is still closed at the AUTOSP sample"* — the boundary triple tooth,
where `AUTOSP`, `SERVLOOP` and `KEYER` all sample within about 30 µs.

`SQUEEZE_MODE_SAMPLED` implements the first half and not the second. It never
arms the same type during an element, and relies on the live paddle state at the
boundary (Priority 3 of `decide_next_element`) to continue a held paddle. That
works while the paddle is still held at the *next* decision, and fails when it is
released in between: the K8 has latched it, we have not.

Reproducing case, family B `(0.25, 2.0, 2.5)`: dit at 0, dah joins at 0.25u, dit
released at 2.0u, both released at 2.5u. K8 sends `..-`, we send `.-`.

## Reproducing

```bash
cc -std=c11 -I../../../components/keyer_iambic/include \
   -I../../../components/keyer_core/include -I../../../test_host \
   -o ours_seq ours_seq.c ../../../components/keyer_iambic/src/iambic.c \
   ../../../test_host/stubs/*.c
python3 sweep.py
```

`k8seq.py` needs `morse8_tb40_nosleep.hex`, built from a fetched `morse8.asm`
per the three patches in `tools/k8/README.md`. It is not in the repository.
