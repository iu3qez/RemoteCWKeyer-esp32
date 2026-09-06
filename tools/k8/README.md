# K1EL K8 — the keying reference, fetched and never vendored

The K8 is the reference this project pins its iambic behaviour against
(issue #32). Its source is **not** in this repository and must not be added to
it. This directory holds the pointer, the checksum and the recipe to reproduce
the oracle locally.

## Why not vendored

The licence, from the header of `morse8.asm`:

> Copyright (C) 1998 Steven T. Elliott. All rights reserved.
> Permission is granted to use, modify, or redistribute this software
> so long as it is not sold or exploited for profit.

Redistribution is permitted, so mirroring would be lawful. It is still not done,
for a different reason: **the no-commercial-exploitation condition is
incompatible with the GPL**, and vendoring the file would attach that condition
to a tree that must stay redistributable under ordinary open-source terms. So we
fetch, verify the checksum, and keep the working copy out of git.

`tools/k8/ref/` is the conventional place for the fetched copy and is ignored.

## What to fetch

| file | sha256 |
|---|---|
| `morse8.asm` | `432df077a19774661c20cc87827d4f1290b7505daa722154b8b3c331a6e0c8de` |

The file is 64176 bytes and carries a timestamp of 1999-09-06 inside the
archive it was distributed in.

**Where to get it.** The file is available on archive.org. No specific snapshot
URL is recorded here on purpose: archive.org is slow and not reliable enough to
sit in the path of a build or a test run.

If we ever need a copy we can depend on, the answer is a **private repository of
our own** holding it. The licence permits that — redistribution is allowed as
long as the file is not sold or exploited for profit — and a private mirror
keeps the GPL conflict away from this tree, which is the whole reason the file
is not vendored here.

Either way, **verify by checksum**, not by where it came from.

Anything that does not match the checksum above is a different file, and the
tests in `test_host/test_iambic.c` that pin K8 behaviour do not describe it.

## Reproducing the oracle

Assembling for `gpasm` needs three patches to the original. None of them touches
the keying logic: two reproduce the programming the source header requires, and
one removes a power-saving feature that is not part of the standard. Keep the
original file untouched and patch a copy.

1. **Rename the label `CONFIG`** (we use `CONFIGM`), because `gpasm` reserves it.

2. **Add the config word**, without which the watchdog resets the part mid-run:
   `CLRWDT` is compiled only under `ifdef BEACON`, which this build does not
   define, while the header requires *Watchdog Disabled, Internal Reset and
   Internal Oscillator enabled*.

   ```asm
   	#include <p12c509.inc>
   	__CONFIG _IntRC_OSC & _WDT_OFF & _MCLRE_OFF & _CP_OFF
   ```

3. **Replace the idle `SLEEP` with `GOTO SERVLOOP`.** The part sleeps when idle,
   and on a 12C509 waking on a pin change is a **reset**, because the chip has no
   interrupts; gpsim 0.32.1 treats that reset as the end of the run. Spinning in
   the service loop instead makes the oracle drivable at any instant.

   *Verified, not assumed.* With the same stimulus the patched and unpatched
   builds produce identical output at `TIMEBASE 40`: first key-down at cycle
   482752, mark 48326 cycles, space 48262 cycles. Without the patch a press has
   to land after the sign-on and before the sleep, and at fast timebases those
   instants nearly coincide — the sign-on's last activity is cycle 338122, so a
   press at 700000 killed the run. With it, a press at 1500000 keys normally, 56
   cycles later.

```bash
gpasm --mpasm-compatible -p p12c509 -o morse8.hex morse8_gpasm.asm
gpsim -i -p pic12c509 -c experiment.stc morse8.hex
```

An experiment script is self-driving: it ends with `break c <cycles>`, `run`,
`log off`, `quit`. Do not run `gpsim -i` without one, or it waits for input.

Pin map, as wired in the experiment scripts:

| pin | signal |
|---|---|
| gpio0 | DIT paddle |
| gpio1 | DAH paddle |
| gpio2 | KEY output |
| gpio3 | push button |
| gpio4 | sidetone |

Inputs are pulled up, so **a closed contact reads 0**.

**Retuning the emulated oscillator is free.** gpsim stimuli are expressed in
cycles and the log counts cycles, so `frequency` does not alter the simulation at
all — it is a scale factor applied when the trace is read. That is what makes it
possible to align the K8's unit with ours exactly, and it is legitimate: the K8's
own clock was a per-chip trimmed RC oscillator, so its absolute frequency was
never a constant of the design.

The speed table is `TIMEBASE = floor(1060 / WPM)`, so the labels are not speeds.
`SPEED_DEFAULT = WPM_26` gives `TIMEBASE 40`, which is 24.83 WPM at a nominal
4 MHz. Pick the timebase nearest the speed you want and let the clock absorb the
remainder.

## Measured constants

At `TIMEBASE 70`, which is about 14.2 WPM, with a 4 MHz clock and therefore
1 µs per instruction cycle:

| quantity | cycles |
|---|---|
| mark | 84566 |
| space | 84382 |
| sidetone period | 1208 |

## What the reference actually says

The verified reading of the sampling schedule, the latch-clearing rule and the
maintainer's decisions all live on **issue #32**, which is authoritative. Read
its comments from the bottom up: each later comment corrects the one before it.

The implementation is `SQUEEZE_MODE_SAMPLED` in
`components/keyer_iambic/src/iambic.c`; the tests that pin it are the
`test_iambic_k8_*` group in `test_host/test_iambic.c`.
