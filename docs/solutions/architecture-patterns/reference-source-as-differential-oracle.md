---
module: keyer_cwnet
date: 2026-09-05
problem_type: architecture_pattern
component: testing_framework
severity: high
applies_when:
  - "keyer_cwnet's conformance to DL4YHF's Remote CW Keyer must be demonstrated"
  - "A protocol behavior rests on a source reading not yet observed on the wire"
  - "The reference is a Windows/Borland binary that cannot be recompiled on this toolchain"
  - "The traffic to capture runs over loopback and Npcap/Wireshark does not intercept it"
root_cause: missing_tooling
resolution_type: tooling_addition
related_components:
  - test_host
tags:
  - cwnet
  - dl4yhf
  - oracle
  - conformance
  - differential-testing
  - protocol
  - tcp-tap
---

# The compiled reference source as a differential oracle

## Context

The host suite had 189 green tests asserting protocol bytes **never compared against the
reference**: the expected value and the implementation both drew from the same `#define`, so
the test measured the implementation against itself. This is the `0x15` episode - our TX sends
keying on `CWNET_CMD_CW_UP = 0x14` and `CWNET_CMD_CW_DOWN = 0x15`
(`components/keyer_cwnet/include/cwnet_client.h:63-64`), which in the real protocol are CI-V
and spectrum; keying actually travels on `MORSE 0x10`. Green by construction, wrong in fact.

The difficulty: the golden standard is a Windows executable compiled with Borland C++ Builder
6, not recompilable here, and its published sources (`Remote_CW_Keyer_Sources.zip`, qsl.net/dl4yhf)
are incomplete - `CwNet.c` includes headers from the author's personal library that are not in
the archive (`StringLib.h`, `yhf_type.h`, `QFile.h`, `YHF_*.h`) and the author does not provide
them.

## Guidance

Three moves, in order of evidentiary strength.

**1. Compile the reference's pure module as a differential oracle.** `CwStreamEnc.c` (the
keying-stream codec) is 230 lines of pure C: it depends only on `<string.h>` and four typedefs
(`BYTE`, `WORD`, `DWORD`, `BOOL`), supplied by a `yhf_type.h` shim. It compiles on the first try
with `clang -I shim`. It does not use `long`, `float` or `double` - only fixed-width integers -
so the two classic sources of cross-compiler divergence (ILP32 vs LP64 data model, x87 vs SSE)
**do not apply**. The input domain is tiny (0–1040 ms in one direction, 0–127 in the other): the
comparison against our own `cwstream_encode_timestamp()` and `cwstream_decode_timestamp()`
(`components/keyer_cwnet/src/cwnet_timestamp.c`) can be made **exhaustive, not sampled**.
Result: zero discrepancies over the whole domain, with UBSan clean.

**2. When its network layer does not compile, you do not need it.** `CwNet.c` (4177 lines, 11
project includes) is out of reach, but its network layer adds no truth: the bytes on the wire
are read by *our own* parser (`cwnet_frame_parse()`,
`components/keyer_cwnet/include/cwnet_frame.h:124`). By feeding the raw streams to our parser
plus *its* codec for the payload, a real capture can be decoded without recompiling a single
line of its networking.

**3. Capture loopback with a TCP tap, not Npcap.** When the DL4YHF client and server run in the
same VM and loopback is not capturable, a transparent TCP relay in Python (the client points at
the tap, the tap forwards to the server) records both directions as raw bytes. No Wireshark, no
Npcap, no sudo, no pcap extraction. The only side effect is TCP re-segmentation, irrelevant
because downstream work happens on the reassembled stream.

## Why it matters

An oracle recompiled from a published source is a **weaker** reference than the shipped binary:
they can diverge (the source's CI-V is demonstrably ahead of the app's). So the oracle is not
the reference - it is a candidate that has to earn the position. Its first test is: *given the
same stimulus, does it reproduce a capture of the real binary byte for byte?* If yes, it has
proven its fidelity on that path and can generate the cases the capture does not cover. If not,
nothing is lost: you have discovered **where** source and binary diverge, which is exactly the
information that was missing.

This inverts the defect of the `0x15` episode: the expected value stops coming from us and
starts coming from a falsifiable external artifact.

## When to apply it

Before asserting how the DL4YHF protocol behaves, and any time a decision rests on a reading of
its source. The source tells you **what to look at**, not what is true: confirmation comes from
the bytes. A corollary that emerged here - `CwStreamEnc.c`'s comment on the end-of-over says
"ten dot-times or 500 ms", but the code (`KeyerThread.c`, `t_us > 14000 * iDotTime_ms`) and the
capture say **14 dot-times**. The comment is as much source as the code, and it can lie; the
bytes are the arbiter.

## Examples

Exhaustive comparison of the codec (the differential oracle):

```c
/* diff_main.c - its codec vs ours, the whole domain */
for (int ms = -50; ms <= 2000; ms++)
    if (CwStreamEnc_MillisecondsTo7BitTimestamp(ms) != cwstream_encode_timestamp(ms)) eb++;
for (int b = 0; b <= 127; b++)
    if (CwStreamEnc_7BitTimestampToMilliseconds((BYTE)b) != cwstream_decode_timestamp((uint8_t)b)) db++;
/* -> ENCODE 0/2051, DECODE 0/128, UBSan clean */
```

```bash
clang -O1 -fsanitize=undefined -I shim -I <ref-src> -I <our-include> \
      diff_main.c <ref-src>/CwStreamEnc.c <our-src>/cwnet_timestamp.c -o difftest
```

The tap that makes an uncapturable loopback capturable:

```
client CWNet (VM) --> tap TCP (Mac, 192.168.179.1:7355) --> server CWNet (VM)
                          |                                   writes both directions
                          '-> sess_N_client_to_server.bin      as raw bytes
                              sess_N_server_to_client.bin
```

The tools (`shim/yhf_type.h`, `difftest`, `cwnet_dump`, `pcap_to_stream.py`, `cwnet_tap.py`) are
unit U2 of the cwnet-bench plan, unblocked, and live in `tools/cwnet/`. The captures produced
are committed together with a `manifest.yaml` per session, from
`tools/cwnet/manifest.template.yaml` - decision from #8, closed on 2026-09-06. A capture
without a manifest is not evidence.
