# tools/cwnet - CWNet test bench tools (U2)

Extraction and decoding of CWNet traffic against the DL4YHF golden standard.
Method and rationale: [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md).

No external dependencies: the scripts are Python 3 stdlib, the C programs
compile with clang/gcc. In particular `pcap_to_stream.py` replaces
`tshark`, which this way does **not** become a CI dependency.

## Our tools

| File | What it does |
|---|---|
| `cwnet_echo.py` | Minimal CWNet server for the determinism loop (R7, #14): echoes the CONNECT with the permissions, PINGs as the requester every 2 s, `RPRT 0` to 0x06 strings, `TX_INFO` with the key taken on the first MORSE byte and released after 1 s, and echoes every MORSE frame back to the sender byte for byte. The `TX_INFO` announces the **callsign** from the CONNECT (`NoCall #n` if empty), not the username: they are two distinct CONNECT fields (92 bytes: 44 username, 44 callsign, 4 permissions) and only the second is what a real CWNet server shows the other clients. The DL4YHF server never echoes MORSE back (H6). Logs both directions like the tap. |
| `cwnet_tap.py` | Transparent TCP relay: the CWNet client points at the tap, the tap forwards to the server and logs both directions as raw bytes. Captures the loopback when Wireshark/Npcap does not intercept it. |
| `cwnet_send.py` | Test CWNet client for the real daemon, `cwnetd` (host/cwnetd): CONNECT, replies to PINGs, sends the raw bytes of a fixture (`--fixture first_over`, ..., `--fixture long` for the jitter measurement) or of a file, prints TX_INFO/RPRT/PING in the clear. It's the "loop without the box" from U6/U7 - see [host/cwnetd/README.md](../../host/cwnetd/README.md). |
| `cwnet_jitter.py` | Measures the deviation (jitter) between `cwnetd`'s virtual output `key`/`ptt` lines and the waits they carry written on them, and the turnaround time after TX (B + tail). Drives `cwnetd` and `cwnet_send.py` on its own; method and measured numbers: [host/cwnetd/README.md](../../host/cwnetd/README.md#measuring-jitter-and-turnaround-time). |
| `pcap_to_stream.py` | Extracts TCP streams from a pcap/pcapng and writes them as raw bytes, one direction per file (stdlib only, handles Ethernet/loopback/SLL/raw). |
| `cwnet_dump.c` | Decodes a raw stream: **our own** frame parser + **DL4YHF's** keying codec. Diagnostic, does not assert. |
| `diff_main.c` | Exhaustive comparison of our `cwnet_timestamp.c` against DL4YHF's `CwStreamEnc.c`, over the whole input domain. |
| `gen_synth.c` | Generates a synthetic MORSE stream with the DL4YHF encoder, to exercise the decoder. |
| `keyer_sim.c` | Simulates DL4YHF's `KeyerThread` (transitions, chronometer adjusted for encoded ms, end of over at 14 dot-times) on the DL4YHF encoder. Source of the synthetic expected values in `test_cwnet_client.c`; case A reproduces the bytes of the first over of session 12. |
| `shim/yhf_type.h` | The four typedefs (`BYTE`/`WORD`/`DWORD`/`BOOL`) that DL4YHF's published archive does not include. Written by us. |
| `shim/Elbug.h` | Stub: `CwStreamEnc.c` includes `Elbug.h` but does not use any Elbug symbol, only the typedefs. The stub avoids having to download `Elbug.{c,h}`. Written by us. |

## DL4YHF source (not vendored here)

`cwnet_dump.c`, `diff_main.c` and `gen_synth.c` compile against DL4YHF's
`CwStreamEnc.c` and `CwStreamEnc.h`. **Permission:** the author (Wolfgang Buescher,
DL4YHF) authorized free use of his sources by email. The `CwStreamEnc.*` module
is plain C and depends only on the shim above.

It is not committed to this repo for now - that is a decision separate from
committing our own tools. To get it:

```sh
curl -sO https://www.qsl.net/dl4yhf/Remote_CW_Keyer/Remote_CW_Keyer_Sources.zip
unzip -j Remote_CW_Keyer_Sources.zip \
  'Remote_CW_Keyer/sources/CwStreamEnc.c' \
  'Remote_CW_Keyer/sources/CwStreamEnc.h' -d ref/
# archive verified: sha256 d960d6b9…, files dated 2025-10-20
```

## Usage

```sh
REF=ref                     # where you put CwStreamEnc.{c,h}
OUR=../../components/keyer_cwnet

# exhaustive codec comparison (must give 0 mismatches)
clang -O1 -fsanitize=undefined -I shim -I "$REF" -I "$OUR/include" \
      diff_main.c "$REF/CwStreamEnc.c" "$OUR/src/cwnet_timestamp.c" -o difftest && ./difftest

# decode a capture
clang -O1 -Wall -Wextra -fsanitize=address,undefined -I shim -I "$REF" -I "$OUR/include" \
      cwnet_dump.c "$REF/CwStreamEnc.c" "$OUR/src/cwnet_frame.c" -o cwnet_dump

# bytes expected from the reference KeyerThread for a list of edges
clang -O1 -Wall -fsanitize=undefined -I shim -I "$REF" keyer_sim.c "$REF/CwStreamEnc.c" -o keyer_sim && ./keyer_sim

# determinism loop: the box points at the echo server, which sends its keying back
python3 cwnet_echo.py --listen 0.0.0.0:7355 --permissions 7 --verbose --record echo
./cwnet_dump echo_1_client_to_server.bin; ./cwnet_dump echo_1_server_to_client.bin   # same MORSE frames in both directions

# live tap: client -> tap -> server, one file per direction
python3 cwnet_tap.py --listen 0.0.0.0:7355 --server <ip-server>:7355 --out sess
# or extract from a pcap:
python3 pcap_to_stream.py cattura.pcapng --port 7355 --out sess
./cwnet_dump sess_1_*.bin

# loop without the box: cwnet_send.py acts as a test client against the real daemon
host/build/cwnetd --listen 127.0.0.1 --port 17355 &
python3 cwnet_send.py --host 127.0.0.1 --port 17355 --fixture first_over --verbose

# jitter and turnaround time measurement (drives cwnetd and cwnet_send.py on its own)
python3 cwnet_jitter.py --cwnetd ../../host/build/cwnetd
python3 cwnet_jitter.py --cwnetd ../../host/build/cwnetd --handover
```

The real daemon (`cwnetd`) and its README - running, flags, how to read a
status line, and the bench procedure with the box (R18) - are in
[host/cwnetd/](../../host/cwnetd/README.md): that README is the place to
look for "how do I run the loop with the real box", this one is where to
look for the tools to build and read captures with.

## Fixtures

Every capture session is accompanied by a `manifest.yaml` next to its
files, filled in from
[`manifest.template.yaml`](manifest.template.yaml). A capture without a
manifest is not evidence: it does not get committed. See the
[test bench plan](../../docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md).
