# cwnetd - CWNet station daemon

CWNet server for PC (Linux or Mac): the remote OM's keying comes in from
the client (the box, or `tools/cwnet/cwnet_send.py` for a test client),
goes out with the timing the client sent, PTT follows the replayed
keying. The core (`components/keyer_cwnet/src/cwnet_server.c`,
`cwnet_play.c`) is the same codec host-tested by the `test_host` suite; around
it here there is only the POSIX layer (`host/platform/`), the virtual output
(`key_output.c`) and the status lines on stdout. Status and edges come out of
two different descriptors, for a good reason: see *Two outputs* below.

For the architecture, see [components/keyer_cwnet/CLAUDE.md](../../components/keyer_cwnet/CLAUDE.md)
and the plan [docs/plans/2026-09-08-2158-feat-station-daemon-plan.md](../../docs/plans/2026-09-08-2158-feat-station-daemon-plan.md).

## Build

```sh
cmake -S host -B host/build
cmake --build host/build
# executable: host/build/cwnetd
# test (socket loopback only, not the server): ctest --test-dir host/build
```

Same strict flags as `test_host` (`-Wall -Wextra -Werror -Wconversion
-Wsign-conversion ...`, see `host/CMakeLists.txt`): one bar, Linux and
macOS.

## Running

```sh
host/build/cwnetd --listen 0.0.0.0 --port 7355
```

`--listen 0.0.0.0` listens on all interfaces: the trust boundary is
the LAN or the VPN (WireGuard, `components/keyer_vpn`), not the process - CWNet
has no authentication. The default port, 7355, is the same as the
reference and the box (`parameters.yaml`, `remote.server_port`).

Ctrl-C (SIGINT) or SIGTERM close the clients and return the output to rest
(key up, PTT off) before exiting.

## Flags

```
--listen ADDR       IPv4 address to listen on (default 0.0.0.0)
--port N            TCP port (default 7355)
--max-clients N     clients served together, max 8 (default 4)
--play-floor MS     buffer floor B (default 100)
--link-ceiling MS   peak-hold beyond which the link is not eligible (default 1000)
--ptt-tail MS       PTT tail after the last key-up (default 100)
--ptt-lead MS       PTT lead on the first key-down, never beyond B (default 0)
--idle MS           silence from the key holder with the key up that releases the key
                    (default 5000; with the key down the PING decides)
--over-max MS       ceiling of an over (default 120000)
--handshake MS      time to complete the CONNECT (default 5000)
--out-cap BYTE      unsent bytes per client beyond which I close it
                    (default 16384, min 256, max 16777216)
--output BACKEND    key and PTT output: virtual (default virtual)
--edges DEST        edges descriptor: 'stderr' or a file (default stderr)
--snapshot-ms MS    period of the state snapshot on stdout
                    (default 5000, min 50, max 3600000)
```

(`host/build/cwnetd --help` is the source, this table is only for
quick reference - if they diverge, trust `--help`. `--help` prints the
defaults by reading them from the same struct the program uses, so it
cannot diverge from the behaviour.)

The defaults are those of the reference and the box (R13): `--ptt-tail
100` is "the box's value" (R9), `--play-floor 100` the minimum B chosen
to absorb LAN/VPN jitter without a perceptible delay.

**`--idle` does not touch a key held down.** Between one element and the
next and between one over and the next it frees a key that nobody is using;
under a key held down it has no say, otherwise it would cut off tuning
after five seconds. The PING decides there: the program answers, not the
operator's hand, so it keeps answering through the whole tuning and stops
when the client dies. Three PINGs without an answer close that client and
the key releases with key up and PTT down at the tail (R8, R16).

**`--out-cap` is a safety net, not a performance feature.** The server sends
a client a few dozen bytes every two seconds: 16 KiB is minutes of
backlog. A peer that has not read in a long time is not coming back, and
holding its bytes only costs the others, who wait for their PING behind it.
Past the cap that client closes, the loop does not slow down.

`--output` has only one backend today, `virtual`: the physical transport
(serial or GPIO to the rig) is behind a Decision not yet opened (KTD9); once
it is, a new backend fills the same two function pointers in
`key_output.h` and nothing above changes.

## Two outputs, and why

**Status** goes out on stdout, non-blocking: if the reader does not keep up
the line is dropped and the next one that gets through confesses it. It's
the right rule for a diagnostic, because a lost line costs a line.

**Edges** do not. They go out on the `--edges` descriptor (`stderr`, or a
file), which never drops one (R11, KTD9). The reason is what those lines
are: they are the jitter measurement. A trace that silently loses exactly
the edges it is measuring is not a worse measurement, it is a wrong
measurement - and it would lose them exactly when the system is loaded,
which is when the number matters.

The two rules - "never drops" and "the loop never stops" - cannot both
hold against a reader that has stopped draining. R11 wins here, and the
cost is made visible instead of hidden:

- **A file cannot stop the loop.** `write(2)` on a regular file never
  returns `EAGAIN` and has no reader to wait for. It's the descriptor
  to use when the numbers matter: `--edges fronti.log` (opened in
  append mode, so a restart adds to the trace instead of erasing it).
- **A pipe or a terminal does.** The write is retried across `EINTR`,
  partial writes, and `EAGAIN` (`poll()` for `POLLOUT`), so the line does
  arrive; but every millisecond of waiting stops the loop. A pipe that
  nobody reads costs nothing until its buffer fills up (16-64 KiB, a few
  thousand edges) and from there on it stops the station. The daemon says
  so while it happens - `stato uscita fronti (...) non drena: N
  ms e aspetto` - and at the end of the session it prints the total wait.
- **The descriptor's flags are never touched.** `stderr` can share the
  open file description with our non-blocking stdout (the shell's `2>&1`
  does exactly this): removing `O_NONBLOCK` from one would remove it from
  the other, making the status lines blocking without it being visible.
  Handling `EAGAIN` works whatever was inherited.
- **The only exception is shutdown.** After SIGINT or SIGTERM the wait per
  edge is capped at one second: a descriptor that nobody drains must not
  make the daemon impossible to close except with SIGKILL. The edges left
  behind end up in the `persi` count of the last line.

`--edges stdout` is rejected at startup: it's the only descriptor that
cannot give edges the separation R11 requires.

## Reading a status line

Status goes out on stdout, one line per event, never blocking: if stdout
does not keep up the line is dropped and the next one that gets through
confesses it (`stato stdout N righe scartate`) - "nothing" and "you weren't
reading" stay distinguishable. Edges are not here: they are on `--edges`
(see *Two outputs*).

**These tables are the contract** with every program that reads the lines,
the station panel ([host/panel/](../panel/)) first of all: a line they do
not describe is a defect, in the daemon or in the tables. Every line starts
with `stato `, is at most 254 characters before its newline (a longer one is
cut there), and is recognised whole, not by prefix. `NOME` is the name the
client announced, escaped: printable ASCII stays, a backslash is doubled,
any other byte becomes `\xNN`. It can contain spaces, ` da `, `:` and `#`,
so a reader finds the fields after it by anchoring on the right. Before the
CONNECT a client has no name, and the lines say `(senza nome)`.

Vocabulary (clients, connections):

| Line | Meaning |
|---|---|
| `stato ascolto ADDR:PORTA max-clients N B>=X ms tetto Y ms coda Z ms lead W ms out-cap C byte` | the daemon is ready, with the configuration it actually has. A reader treats it as a new daemon: what it knew before belongs to another process |
| `stato uscita BACKEND fronti DEST` | where the edges end up (a separate line: a long path must not truncate the configuration) |
| `stato uscita serial tasto LINE ptt LINE porta DEVICE fronti DEST` | the same, for `--output serial`: the line of each function as `--key-line` and `--ptt-line` gave it, and the port. `DEVICE` is escaped as `NOME` is, and ends at the first ` fronti ` |
| `stato accettato client N da IP:PORTA` | TCP accepted, waiting for the CONNECT |
| `stato rifiutato da IP:PORTA: nessuno slot libero` | beyond `--max-clients`, closed immediately (R1: accept never blocks) |
| `stato connesso client N NOME da IP:PORTA` | CONNECT complete, the client is READY |
| `stato disconnesso client N NOME da IP:PORTA: MOTIVO` | TCP closed. `MOTIVO` is one of `nessuno slot libero`, `CONNECT di lunghezza sbagliata`, `errore di parse`, `stringa 0x06 senza NUL`, `CONNECT non arrivato in tempo`, `tre PING senza risposta`, `invio fallito`, `chiuso dal peer`, `arresto` (the daemon is stopping) |
| `stato client N lettore fermo: B byte non inviati, chiudo` | that client stopped reading and its unsent bytes reached `--out-cap`: it is closed, and a `disconnesso` line follows |
| `stato slot client N ancora in uso da IP:PORTA: chiudo la connessione precedente` | the core handed out a slot the daemon still had open, because the event that closed it was among the lost ones: the old connection is closed before the new one takes the slot |
| `stato accept fallita: ERRORE` | accepting a connection failed for a reason other than "nothing pending"; `ERRORE` is the system's text |

Vocabulary (key, PTT, link, over):

| Line | Meaning |
|---|---|
| `stato chiave client N NOME` / `stato chiave libera` | who holds the key now (arbitrated, one holder at a time) |
| `stato ptt 1` / `stato ptt 0` | the level of the output PTT, written every time it changes: after each batch of core events, once the output is aligned to the core, and after the release at shutdown. It is a level, not an edge: the instant the edge was scheduled for is on `--edges`. With the default tail the PTT drops at every word gap, and below about 36 WPM between letters too: the key holder, not the PTT, says who is transmitting |
| `stato latenza client N X ms peak Y ms` | RTT of the last PING and its peak-hold |
| `stato link non idoneo client N NOME: peak Y ms` | the peak-hold has exceeded `--link-ceiling`: that client does not take the key |
| `stato over client N NOME B X ms` | an over has started, with the B computed for that session |
| `stato byte in ritardo client N NOME: B byte, M ms in totale` | a byte arrived after the deadline of the edge it carried: the element came out longer than it was keyed, and the link is slipping. Cumulative, so two lines apart say *how fast* |
| `stato fault [client N NOME:] MOTIVO` | FAULT philosophy: key up and it stops (holder vanished mid-over - TCP closed or three PINGs without an answer -, over too long, holder silent past `--idle` with the key up). A fault says nothing about who holds the key: a `chiave` line says that |
| `stato fault: uscita DEVICE: CAUSA` | the serial output failed, and the daemon stops right after it with exit code 3: the port is in a state nobody knows. `CAUSA` is `porta scomparsa (hang-up)`, `porta scomparsa (read: ERRORE)` (the adapter was unplugged), `TIOCMSET: ERRORE dopo N ms` (a line change failed) or `TIOCMSET lento: N ms` (a line change took over 100 ms). No line is driven after it: closing the port drops both lines together |
| `stato eventi persi N` | the core produced more events than the read buffer could hold, `N` in that batch: no edge is lost, only the descriptive lines, so a reader rebuilding the state from the lines is wrong until the next snapshot |

Vocabulary (the daemon itself):

| Line | Meaning |
|---|---|
| `stato stdout N righe scartate` | the confession: `N` status lines, counted since start, that stdout did not take. It is written right before the next line that gets through, and only when `N` has grown since the last confession |
| `stato arresto` | SIGINT, SIGTERM or an output FAULT: the daemon is stopping. What follows belongs to the shutdown: the `disconnesso ...: arresto` lines, `stato ptt 0` if the PTT was on, the two totals below |
| `stato stdout N righe scartate in totale` | at shutdown, if any line was dropped: the same count as the last confession, not a new one |
| `stato uscita fronti (DEST): A attese per M ms, E errori, P persi` | at shutdown, if the edges descriptor ever waited or failed: `P` other than zero is the only case where an edge was not written, and it only happens after a SIGINT |
| `stato uscita fronti (DEST) non drena: N ms e aspetto` | the edges descriptor has stopped taking bytes and the loop has been stuck there for N ms (see *Two outputs*) |
| `stato uscita fronti (DEST) rifiuta: ERRORE` | the edges descriptor refused a write (a closed pipe, a full disk): that edge counts as an error, and the next one tries again |
| `stato poll fallita: ERRORE` | waiting on the sockets failed for a reason other than a signal: the daemon stops |

### The periodic snapshot

The lines above describe events, and any of them can be dropped. A reader
that lost `stato chiave libera` would keep showing the previous key holder
with nothing to tell it otherwise. So every `--snapshot-ms` (default 5000)
the daemon also writes the whole state, over several lines, because 8
clients with their names do not fit in one:

```
stato snap inizio v2 istanza S-P seq N client K chiave C ptt L periodo MS
stato snap manopole max-clients N B>= MS tetto MS coda MS lead MS idle MS over-max MS handshake MS out-cap BYTE
stato snap client I/K IDX STATO ADDR lat MS peak MS nome LEN NOME      (K lines)
stato snap uscita cambi N max-us US lenti N
stato snap fine seq N
```

| Field | Meaning |
|---|---|
| `v2` | the version of this vocabulary. It changes when a line in these tables changes shape, so a reader can say it no longer knows what it is reading. `v2` added `stato snap uscita`, the serial form of `stato uscita` and the output fault |
| `istanza S-P` | which daemon wrote it: `S` its start instant in seconds since the Unix epoch, `P` its pid. The pid alone repeats in a container. Another value means another process, even when its `stato ascolto` line was lost |
| `seq N` | counts snapshots from 1. It grows for every snapshot the daemon writes, taken by stdout or not, so a gap between two complete snapshots means some were lost; `fine` carries the same `N` |
| `client K` | how many `stato snap client` lines follow: the connections with an open socket |
| `chiave C` | the key holder's index, or `libera` |
| `ptt L` | the output PTT level, the same as the last `stato ptt` line and the last `ptt` edge on `--edges` |
| `periodo MS` | the snapshot period: a reader that sees nothing for three periods can say the daemon is silent |
| `manopole ...` | the settings in force, in the same units as the flags: `max-clients`, `B>=` (`--play-floor`), `tetto` (`--link-ceiling`), `coda` (`--ptt-tail`), `lead`, `idle`, `over-max`, `handshake` in ms, `out-cap` in bytes |
| `I/K` | the position of this line among the `K` |
| `IDX` | the client index, the `N` of the event lines |
| `STATO` | `pronto` when the client has completed the CONNECT; `attesa` when the socket is open and it has not, which includes a client the core has already closed while the event that said so was lost |
| `ADDR` | `IP:PORTA`, or `?` when the system could not say |
| `lat`, `peak` | the RTT of the last PING and its peak-hold, in ms; `-1` until a PING has come back |
| `uscita cambi N max-us US lenti N` | the output's line changes since start: how many, the slowest in microseconds, and how many took over 100 ms (each of those is a FAULT). All zero for `virtual`, which drives no line |
| `nome LEN NOME` | the name, escaped as above and empty before the CONNECT, last on the line, preceded by its length in characters. A name shorter than `LEN` was cut by the 254-character limit |

The lines of one snapshot are written in one go, with no other line between
them. A reader applies a snapshot only when it has all of it: the opening,
`manopole`, exactly `K` client lines with positions `1/K` to `K/K` in order,
`uscita`, and `fine` with the same `seq`. With anything else between them, or a line
missing, it discards the snapshot and waits for the next one. The name's
declared length is what lets a reader tell a name containing `stato stdout`
from a confession glued after half a line, which is what stdout does when
it is a socket that takes part of a write (journald).

The snapshot is one more deadline of the loop, not a tick: when idle it is
the only thing that wakes the daemon, and at most one is written per period.
It is written after the core's work of that pass, and never in the 2 ms
before a key or PTT edge is due: then it waits for that edge, never longer
than one period. At the default period, an idle daemon writes three lines
every 5 s, about 4 MB a day into a file.

**Neither an empty FIFO nor a key held down is a fault.** An empty FIFO is
the normal state of a live over (R8): every byte arrives about B ms before
its own deadline, and every element longer than B empties it. A key down
with nothing arriving is tuning, which at low power is standard procedure:
the engine never lifts it on its own, because keying silence does not
distinguish who is holding down from who has dropped. That distinction is
made by the PING, and the fault seen with the key down is the holder
declared dead. The signal that arrives *before* that, when the link starts
slipping but the bytes are still arriving, is `stato byte in ritardo`.

Key and PTT output (`key_output.h`, `virtual` backend) - **not on stdout**:
on the `--edges` descriptor, which defaults to `stderr`:

```
key 1 431839006        <- key down, scheduled for instant 431839006 (process monotonic ms)
key 0 431839102        <- key up
ptt 1 431838958         <- PTT on
ptt 0 431839786         <- PTT off
```

The instant is the *scheduled* one (B plus the sum of the waits decoded
from the wire), not the one at which the line was written: this way an
over reconstructs itself from the lines alone, and it's the number the
jitter measurement looks at from the outside (see below). An edge that
does not change state is not an edge: it does not generate a line
(`key_output.h`). None of these lines is ever dropped - that's the entire
reason they have a descriptor of their own.

## The station panel

To see the state in a browser instead of following the lines as they
scroll: [host/panel/](../panel/README.md). It reads these lines from a file,
the journal or a pipe, and never talks to the daemon. Run the daemon with
its stdout appended to a file (`>> cwnetd.log`) and `--edges` to another
file, and point the panel at the first.

Whoever reads stdout by eye sees the snapshot lines every 5 s among the
events; `--snapshot-ms` spaces them out, at the price of a page that takes
longer to correct itself after a lost line.

## Loop without the box

Quick test, no hardware: a Python client in place of the box.

```sh
host/build/cwnetd --listen 127.0.0.1 --port 17355 --edges /tmp/fronti.log &
python3 tools/cwnet/cwnet_send.py --host 127.0.0.1 --port 17355 --fixture first_over --verbose
cat /tmp/fronti.log
```

Expected on the virtual output (AE2, `ref_first_over`, B=100 ms by
default): four key edges (down at +100, up at +148, down at +196, up at
+340 from the arrival of the first byte) and two PTT edges (on with the
first key-down, off 100 ms after the last key-up). Without `--edges` they
end up on stderr, i.e. on the terminal together with the status. Details
and other scenarios:
[tools/cwnet/README.md](../../tools/cwnet/README.md).

## Bench with the box (R18)

This is the step CI does not do: the maintainer runs it, with the real
box (the ESP32 keyer, with a key or a paddle attached) as the client.
`tools/cwnet/keyer_sim.c` gives the reference stimulus -
the bytes the *official client* would send for a known keying -
so there's something encoded to compare the real edges against.

### 1. Build and start the daemon on the PC

```sh
cmake -S host -B host/build && cmake --build host/build
host/build/cwnetd --listen 0.0.0.0 --port 7355 --edges /tmp/fronti.log
```

(`--edges` to a file: this way the edges stay readable even while the
status scrolls, and a file cannot stop the timing loop.)

Note the IP address of the PC on the same network/VPN as the box (LAN or
WireGuard - do not expose `cwnetd` on the Internet, CWNet does not
authenticate).

### 2. Point the box at the daemon

From the box's serial console (USB-CDC, `components/keyer_usb`) or from
its web UI, **Remote** section:

```
set remote.server_host <PC IP>
set remote.server_port 7355
set remote.username <any name>
set remote.cwnet_enabled true
```

`system.callsign` is the callsign that ends up in the CONNECT (the field
that `cwnet_echo.py` now announces - see `tools/cwnet/README.md`); if you
haven't already set it:

```
set system.callsign <your callsign>
```

These parameters are `runtime_change: reboot` (`parameters.yaml`): restart
the box so it connects with the new values.

### 3. Confirm the connection

On `cwnetd`'s stdout this should appear:

```
stato accettato client 1 da <IP scatola>:<porta>
stato connesso client 1 <il tuo nominativo> da <IP scatola>:<porta>
```

If it does not appear: check that the box and the PC can see each other
on the network (ping), that the port is the same on both sides, and that
`remote.cwnet_enabled` is actually `true` after the restart (`show
remote.*` in the console).

### 4. Key and read the edges

Set the box to 25 WPM (`set keyer.wpm 25`, dot = 48 ms) and send the
letter "A" (di-dah) with the paddle, then leave the key still.

In the `--edges` file this should appear, in order, the same shape as
AE2/`ref_first_over` (already pinned by `test_cwnet_play.c`):

```
ptt 1 <t0>          <- PTT on with the first key-down
key 1 <t0>          <- key down (the "di")
key 0 <t0+48>        <- key up, ~48 ms later (a dot at 25 WPM)
key 1 <t0+96>        <- key down (the "dah")
key 0 <t0+240>        <- key up, ~144 ms later (a dash at 25 WPM)
ptt 0 <t0+340>        <- PTT off, 100 ms (--ptt-tail) after the last key-up
```

`keyer_sim.c` generates the same stimulus in byte form (scenario A of its
`main()`: `run("A primo over, dot 48", ...)`, which prints `80 24 A4 3C
60`); `decode7()` on those bytes gives the same waits, 48/48/144 ms. The
hand does not reproduce exact 48.000 ms like `keyer_sim.c` - the
comparison is on *shape* (four edges, the intervals close to 48/48/144 ms,
PTT on with the first down and off 100 ms after the last up), not on a
millisecond-level deviation: that is what the jitter measurement below
does, with no hand in between.

For a byte-exact comparison (not just the shape), capture the raw traffic
while you key with `tools/cwnet/cwnet_tap.py` (or a pcap with
`tools/cwnet/pcap_to_stream.py`) and decode it with
`tools/cwnet/cwnet_dump.c`: the captured MORSE bytes must decode to the
same waits that `keyer_sim.c` writes for the scenario you reproduced.

### 5. Write up the result

The bench step closes only once it's written up on
[#64](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/64): a comment
with what was keyed, the stdout lines observed (or a snippet), and
whether the expected shape holds.

## Measuring jitter and turnaround time

`tools/cwnet/cwnet_jitter.py` runs the loop without the box with a long
sequence (`cwnet_send.py --fixture long`, 104 elements, the letter "V"
repeated) and compares every `key` line against the instant it carries
written above - see the script's docstring for the exact method and why a
calibration is needed between the two processes' clocks. This is not a
measurement with an oscilloscope on the real key (that's the step with the
box above): it's a deviation measured on one machine, not an RT
guarantee.

```sh
python3 tools/cwnet/cwnet_jitter.py --cwnetd host/build/cwnetd
python3 tools/cwnet/cwnet_jitter.py --cwnetd host/build/cwnetd --handover
```

(The script reads the edges from the daemon's `stderr`, which merges into
the same pipe as the status: that's the default of `--edges`, and the two
processes' monotonic epochs are calibrated on the first edge - see the
docstring.)

### Measured numbers

**macOS, Apple Silicon** - MacBook Pro 18,2 (Apple M1 Max), macOS 26.6.2
(Darwin 25.6.0, arm64), system Python 3, 2026-09-10, `--play-floor 100
--ptt-tail 100` (the defaults):

| Measure | Value |
|---|---|
| Average deviation (jitter), 9 runs of 104 edges | between 0.39 and 0.64 ms |
| Maximum deviation, same 9 runs | between 1.2 and 4.7 ms |
| Edges received | 104/104 in every run |
| Station interval: last key-up -> PTT down (`--handover`) | 100 ms (= `--ptt-tail`, measured over 5 runs) |
| Turnaround time after TX (client -> PTT down at the station) | B + tail = 100 + 100 = **200 ms** |

Turnaround time is the quantity that matters to whoever uses the original
program (the delay between "the remote operator releases the key" and
"the station's PTT drops"): it's a sum of configuration (B, the buffer
floor, plus the PTT tail), confirmed on the loop by measuring exactly the
interval between the last key-up and the PTT down of a session that closes
the over correctly (`ref_first_over`).

**It used to be 150 ms, now it's 200.** The floor of B went from 50 to
100 ms, and this number follows it: it's the extra 100 ms paid for not
cutting off an element when the network jumps. Whoever has a stable link -
LAN, or a VPN over fibre - brings it back to where it was with
`--play-floor 50`, and gets back 150 ms; it's a configuration choice, not a
limit of the program. The floor is not the delay: if the holder's
peak-hold is higher, B is that, and turnaround time rises accordingly.

**Linux, x86-64 on bare metal** - `sf-B450M-DS3H-V2` (AMD Ryzen 7 5700G),
Linux 7.0.0-31-generic x86_64, 2026-09-12, the same defaults. It's the
station machine, booted from an Ubuntu disk:

| Measure | Value |
|---|---|
| Average deviation (jitter), 3 runs of 104 edges | between 0.25 and 0.47 ms |
| Maximum deviation, same 3 runs | between 0.62 and 1.07 ms |
| Edges received | 104/104 in every run |
| Station interval: last key-up -> PTT down (`--handover`) | 100 ms (= `--ptt-tail`) |
| Turnaround time after TX | B + tail = 100 + 100 = **200 ms** |

**Measured on an idle machine**, with no other load running. Nobody has
measured what happens under load, and that's the case that matters for a
station that also does other things: whoever puts a browser, a capture, or
a backup on it should redo the measurement instead of trusting this line.

With that reservation: more faithful than the Mac, and not by a little,
because the worst maximum here sits below the best of that one. Two
machines alone don't make a law, and neither is a measurement with an
oscilloscope on the real key - that remains the bench step with the box.

A note on the method, because it differs between the two systems: the
script calibrates on the first edge and measures the drift from there. On
macOS it **must** do this, because Python's `time.monotonic()` and the
daemon's `CLOCK_MONOTONIC` do not share the epoch; on Linux they do, so
there the calibration is not needed and hides nothing. The numbers in the
two tables remain comparable because they measure the same thing, the
drift edge by edge.
