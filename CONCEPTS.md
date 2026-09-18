# Concepts

> Shared domain vocabulary for this project - entities, named processes, and status concepts with project-specific meaning. Seeded with core domain vocabulary, then accretes as ce-compound and ce-compound-refresh process learnings; direct edits are fine. Glossary only, not a spec or catch-all.

## Protocol

### CWNet
The network protocol of DL4YHF's Remote CW Keyer, which carries CW keying, audio, radio control and telemetry over a single TCP connection. It is the golden standard against which the project proves its compatibility: we do not make a clone of it, we replicate its observable behaviour on the CW path.

### Golden standard
The reference implementation - the DL4YHF client and server, running - against which every behaviour that matters is *proven*, not *made similar*. Its published source tells you what to look at, not what is true: the truth is the bytes on the wire, not the code or its comments.
*Avoid:* reference (when ambiguous).

### Differential oracle
An **executable** artifact derived from the reference - one of its modules recompiled, or its firmware run in an emulator - that receives the same stimulus as our code so that the two outputs can be compared. It is not the golden standard: it is a candidate that earns its place by proving that it reproduces a capture of the true reference. When it answers with a count of divergences instead of a verdict, that count is a gate and not a gradient.

## Keying

### Keying stream
The lock-free ring in RAM through which every keying event of the box passes: a single producer, the real-time task, feeds it on every tick and writes a slot only when the state changes; each consumer reads from its own index, never blocking the producer or the other consumers. It is the only interface between whoever produces keying and whoever consumes it: no other shared state, no callbacks.
*Avoid:* stream (when ambiguous with the CWNet flow), buffer.

A consumer that falls a full lap behind is in overrun: the slot the producer is about to overwrite is not readable, and the consumer realigns to the oldest position still readable. A sample equal to the previous one does not take a slot: the producer counts the ticks of silence and delivers them as a marker, so a consumer rebuilds stream time without one sample per tick. It is not the [MORSE keying stream](#morse-keying-stream), which is the encoding of keying on the CWNet wire.

### MORSE keying stream
The CW keying flow carried by CWNet as a sequence of 7-bit bytes - not text, not an absolute timestamp per event. Each byte carries the key state (down/up) and the wait time before applying it. It travels on its own dedicated command, separate from those for CI-V, spectrum and audio.

### 7-bit timestamp
The non-linear encoding of the wait time between two key transitions: fine resolution for short intervals, progressively coarser for long ones, so as to cover a wide time span in seven bits. The codec is lossy by construction on long intervals - the protocol itself quantizes the timing.

### Over
A continuous transmission turn. Its end is signalled by a second key-up command after a silence threshold has elapsed; the silence *between* one over and the next does not travel on the wire, so keying determinism can be demanded within an over, not across different overs.

### Key holder
The client whose keying the server plays back on the rig: only one at a time. The key is taken with the first MORSE byte when it is free, and the server announces it to everyone with a TX_INFO (client index and callsign; «-- nobody --» when it is free). On the client side the state is free, mine or someone else's; on the server side, when the key goes free again is a station policy of ours.

## Latency

### PING
The round-trip time measurement exchange, in three phases with three timestamps: the initiator marks the departure, the peer inserts its own, the initiator marks the arrival. The difference is always computed on the initiator's clock - the peer's timestamp serves diagnostics only, never a subtraction.

### Peak-hold latency
The latency value shown and used to size the jitter buffer: it rises immediately on every new peak and falls slowly, so it diverges from the instantaneous round-trip value in the presence of jitter. It is a control parameter, not an instantaneous measurement.

## Station

### Status snapshot
The group of lines the station daemon writes to stdout at a fixed period, even when idle, with the complete state: instance, key holder, output PTT, settings and one client per line. The other status lines are events and can be lost; the snapshot is the layer from which a reader that lost some, or that started late, gets back to the true state. It counts only if it arrives whole: a snapshot missing a line is discarded.
*Avoid:* riepilogo (in English text).

## Flagged ambiguities

- "keying stream" was used both for the ring in RAM and for the CWNet flow of 7-bit bytes: the ring is the Keying stream, the flow on the wire is the MORSE keying stream.
