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

Assembling for `gpasm` needs exactly one patch to the original: the label
`CONFIG` must be renamed (we use `CONFIGM`), because `gpasm` reserves it. Keep
the original file untouched and patch a copy.

```bash
gpasm --mpasm-compatible -p p12c509 -o morse8.hex morse8_gpasm.asm
gpsim -i -p pic12c509 -c experiment.stc morse8.hex
```

Pin map, as wired in the experiment scripts:

| pin | signal |
|---|---|
| gpio0 | DIT paddle |
| gpio1 | DAH paddle |
| gpio2 | KEY output |
| gpio3 | push button |
| gpio4 | sidetone |

Inputs are pulled up, so **a closed contact reads 0**.

The sign-on message runs at power-up until roughly cycle 591556 with the
transmitter squelched. Drive the paddles only after that, or the first elements
are swallowed.

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
