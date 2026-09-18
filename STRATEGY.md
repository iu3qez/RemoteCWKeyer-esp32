---
name: RemoteCWKeyer-esp32
last_updated: 2026-09-08
---

# RemoteCWKeyer-esp32 Strategy

## Purpose

The operator wants to key a remote rig with a real paddle. Today the only
chain that speaks CWNet (the protocol of DL4YHF's Remote CW Keyer) is one
Windows PC per side, and whoever wants to rebuild a piece of it has no
reference to prove compatibility against: not for the protocol (source that
does not compile, no spec), not for the keyer timing. The DL4YHF program
has a different purpose from ours and cannot be bent: Borland, Windows only,
GUI code not available, the whole CI-V baggage in tow, debatable station
choices (PTT held 500 ms). The project has stalled twice for the same
reason: a valid and fast test method was never set up.

## Positioning

Every behaviour that matters has a real reference and is proven against it,
not made similar: the CWNet wire against the original DL4YHF client and
server, the keyer against the executed K1EL K8: human input → K8 output,
reproducible, up to 40 WPM. The K8 is the reference for the feel, not for
the implementation: the limits of a 1998 PIC12 are not ours.
The DL4YHF program is the reference for the protocol, not for the product:
the product is our two ends, the server and a client that is the box or a
program on a PC, and the station choices (PTT, key handover, playback
buffer) are ours, made once because we own both ends. CWNet also carries
audio, CI-V and
spectrum: we use it for keying and for what keying needs; the rest of the
station is not committed to going through it.
TX and RX grow together because the TX→RX chain in loop, on the same
hardware or on two, is the test bench: nothing is done until it passes there.

## Users

**Primary:** the IO4A contest team OM who takes part remotely with their own
paddle - hires the box to sit at the team's CW operating position without
wiring an RS-232, without configuring VPN and audio redirect, without a
Windows PC in between. Station side: Orion MkII + Thetis, and our server on
a Linux or Mac PC in place of the DL4YHF program.

**Primary, client on PC:** the OM who keys from a Windows PC with the paddle
on the control lines of a serial port, without the box. Same client core,
same keyer FSM.

**Secondary:** the developer/tester - the only user until the reference is
proven. Their tool is the serial console, not the WebUI.

## Boundaries

- WireGuard: added because "it is there and costs little", never tested; in
  a contest the VPN on the PC is needed anyway. Dropped if it hurts.
- Presets other than the K8 (Curtis A/B, Winkeyer, Ultimatic): the logic
  stays configurable along axes and is not tied to the K8, but a value that
  no executable reference proves is not populated. Best effort, no investment.
- Above 40 WPM the K8 is no longer the reference: the configurable windows
  are best effort, without a metric.
- No DL4YHF clone: compatible on the wire, not a replica of everything. This
  holds even more now that both ends are ours on the PC too: CW-only, no
  CI-V, no audio inside CWNet.
- Client on Mac: best effort. The host client is portable, Windows first
  target, Linux after; the paddle comes in through the control lines of a
  USB serial port, and with the Mac's system driver the latency timer stays
  at 16 ms. No metric, no investment.
- K8 feel on the PC client: no metric until the jitter of the 1 ms tick on
  Windows is measured. The metric stays with the box.
- Audio, CI-V and spectrum inside CWNet: no commitment. Our server
  implements the commands that keying requires; the rest of the station
  goes through wherever it goes today.
- No box as server: for IO4A the server is a daemon on a station PC. A box
  with a dual client/server personality is a useful repurpose, for tests
  too, but not now.
- No OTA now: update via web flasher (separate repo) + USB. If it comes
  later, so much the better.
- WebUI: no investment until the test bench, CWNet and K8 hold.
- The serial log on ESP32 blocks real time: no blocking log on the RT path,
  ever.

A boundary forbids **building**, not **remembering**. Opening an issue about
something outside the boundaries is not investment: it is how we avoid
rediscovering it from scratch in six months, and how we know what is
waiting when the boundary moves. What the boundary excludes is scheduling -
parked work does not jump ahead of the test bench, CWNet and K8.

The tracker also records what we will not do now. A backlog that holds only
authorised work is not discipline: it is amnesia.

_Resist a change when:_ the only argument is "it is there and costs little
to add", it cannot be proven against the reference (DL4YHF client, executed
K8), or it reproduces a station choice of the DL4YHF program only because
the reference makes it.

## Key metrics

- **CWNet conformance** - the test loop against the official DL4YHF
  client/server passes or not, in both roles: the box's client against the
  DL4YHF server, our daemon against the DL4YHF client. It lives in
  `test_host` plus a bench with the Windows program. It has been the most
  painful part: metric number one.
- **K8 feel** - on a corpus of real keying up to 40 WPM, our element
  sequence matches that of the executed K8, stable under the phase of the
  stimulus: passes or not. It lives in the keyer-logic repo, as its CI gate;
  here we read which pinned commit passed it.
  Timing stays tolerant as written in
  [docs/k8-timing-tolerance.md](docs/k8-timing-tolerance.md): one tick,
  1000 µs, at speeds aligned to the tick.
- **RT ceiling** - worst case in µs of one loop iteration on Core 0
  (GPIO → iambic → stream → audio); limit 100 µs from ARCHITECTURE.md.
  Gate not yet proven: today it is not instrumented.

## Tracks

### Test bench

The fast test method that never existed: loop against the DL4YHF
client/server, capture of the paddle levers from the box for the keyer
corpus, RT instrumentation, serial console as a working tool (log, filters,
WiFi, test commands, non-blocking).

_Why it serves the approach:_ without this, "exact" cannot be proven, and
the project has already stalled twice for lack of it.

### CWNet, both ends

Client on the box: TX, then RX, against the DL4YHF server. Our server as a
daemon on a station PC, Linux or Mac, against the DL4YHF client, with
station policies decided by us. The daemon is in C, in this repo, on the
same `keyer_cwnet` codec compiled for host: one wire, one test. The bench's
echo server stays the RX end of the test loop and is the seed of the
daemon. The client also exists as a portable host program, Windows first
target, with the paddle on the control lines of a serial port: same
`keyer_cwnet` core and same submodule FSM; `cwnet_socket` is the only
platform file. The client-daemon loop on the same machine is the bench
without the box.

_Why it serves the approach:_ the wire belongs to the golden standard, the
station policies are ours: owning both ends is how we decide them once and
prove them in loop.

### Keyer

The keyer logic lives in its own repo, consumed here as a submodule at a
pinned commit. The interface (`iambic.h`, `sample.h`) and the RT engine
belong to this repo and are not touched from there; the rest is that
repo's. Here we do the bump and supply the lever capture.

_Why it serves the approach:_ the K8 feel is proven with a tool of its own,
without stopping CWNet and without CWNet stopping it.

### Frictionless operating position

Reference hardware shipped already flashed and personalised, Winkeyer USB
for N1MM, a network that asks the OM for no configuration. WebUI only as a
configuration tool, frozen until the first three tracks hold.

_Why it serves the approach:_ it is the reason the IO4A OM leaves the
Windows client; without it, the proven reference stays an exercise.

## Brand

**One-liner:** We are radio amateurs: this is for fun. It is ready when it
is ready.
