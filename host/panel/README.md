# Station panel for cwnetd

A page in the browser that shows who is connected to `cwnetd`, who holds the
key, whether the output PTT is on and which settings the daemon runs with,
and that says so when it cannot guarantee what it shows.

The panel is a separate program. It reads the daemon's status lines and
never talks to the daemon, so it can be started, stopped and restarted at
any time without touching the station: the daemon's loop is its timing, and
nothing here runs inside it (Decision
[#65](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/65)). Python 3.9
or later, standard library only: nothing to install.

The lines it reads are described in
[host/cwnetd/README.md](../cwnetd/README.md), "Reading a status line". That
table is the contract between the two programs.

## Starting it

The way to run it: the daemon appends its status to a file, and the panel
follows the file by name.

```sh
host/build/cwnetd --listen 0.0.0.0 --edges /var/log/cwnetd-fronti.log >> /var/log/cwnetd.log &
python3 host/panel/cwnetd_panel.py --follow /var/log/cwnetd.log
```

Then open <http://127.0.0.1:7356/>.

- **`>>`, not `>`.** With `>` the daemon keeps its own write offset, and
  after a truncation the file fills with NUL bytes up to it. The panel skips
  them, but the file is then as large as before.
- **Rotate only with copytruncate** (`copytruncate` in logrotate). `cwnetd`
  never reopens its stdout: after a rotation by rename it keeps writing into
  the renamed file, while the panel, which follows the name, moves to the
  new empty one and calls a live daemon silent until the daemon restarts.
- **A restart onto a new file is followed.** When the file under the name
  changes, the panel reads what is left of the old one and then the new one
  from its start.
- At start-up the panel reads the last 64 KiB of the file. What it finds
  there sets the state but not the daemon's liveness: until a new line
  arrives the page says it is waiting. Events found there show `dal file`
  in place of a time, because their time is not known.
- The file grows by three lines every 5 s with the daemon idle, about 4 MB
  a day at the default `--snapshot-ms`.

### From the journal

Under systemd, with the daemon writing to the journal:

```sh
journalctl -f -n 0 -o cat -u cwnetd | python3 host/panel/cwnetd_panel.py
```

The journal can lose lines without saying so, when its rate limit cuts in:
the next snapshot corrects the page within one period, and the gap shows
in the events as missing snapshots. Its socket can also take half a line;
the daemon then counts the line as dropped and the panel reads the half as
unreadable.

### From a pipe

```sh
host/build/cwnetd --edges /var/log/cwnetd-fronti.log | python3 host/panel/cwnetd_panel.py
```

It works, and it costs two things. A pipe cannot be reopened: a panel that
dies leaves the station without a page until the daemon is restarted too.
And a panel slower than the daemon makes the daemon drop status lines,
which it confesses and the page reports.

### Never `2>&1` towards a pipe or the journal

The daemon's key and PTT edges go to stderr unless `--edges` names a file,
and they are never dropped: when stderr stops draining, the daemon waits.
Merged into a pipe or the journal that the panel reads, a slow panel stalls
the daemon's edge writes, and the station with them. Give `--edges` a file.
If the panel reads stdin and sees edge lines among the status lines, it
says so in a banner. Into a regular file, as in the first example, `2>&1`
is harmless, and a panel that follows a file does not raise the banner.

## Who can see the page

By default the page is served only on this PC (`127.0.0.1`). To serve it
to the LAN or the VPN:

```sh
python3 host/panel/cwnetd_panel.py --follow /var/log/cwnetd.log --listen 0.0.0.0
```

Whoever reaches that address sees the connected callsigns, their IP
addresses and ports, and the daemon's settings. There is no password and no
TLS: open it only towards a segment you trust, as for the daemon itself.
The page commands nothing, and it never shows the path of the edges file.

The page answers only a browser that asked for it by a name it expects:
`localhost`, `127.0.0.1` or `[::1]` when listening on this PC, any IP
address when listening on another one. That stops a page elsewhere from
reaching the panel through DNS rebinding. To use a host name, name it:

```sh
python3 host/panel/cwnetd_panel.py --follow /var/log/cwnetd.log --listen 0.0.0.0 --allow-host station.lan
```

The port is not checked, so an SSH tunnel works:
`ssh -L 8080:localhost:7356 station`, then <http://localhost:8080/>.

## Flags

```
--follow FILE       follow this file by name, like tail -F (without it: stdin)
--listen ADDR       address to serve the page on (default 127.0.0.1)
--port N            TCP port of the page (default 7356)
--allow-host NAME   a host name the browser may use; repeatable
```

## What the page says

The key holder is the steady "who is transmitting": it stays the same for
the whole over. The PTT shows the real output level, and with the default
100 ms tail it drops at every word gap, and below about 36 WPM between
letters too.

The Link column says whether a client may take the key: `idoneo` when its
peak latency is at most the link ceiling, `non idoneo` when it is over, as
the daemon decides. It shows `-` until both the peak and the link ceiling
are known: the ceiling arrives with `stato ascolto` or the first snapshot.
A client that answered PINGs but never inside the 2 s measurement window
also shows `-`, although the daemon refuses it the key: when it tries, the
events show `link non idoneo` with `peak -1 ms`.

Key holder and PTT are shown as certain only when the daemon is alive and
the state is guaranteed. Otherwise they stay on the page, greyed, as the
last known values. The banners stack, the ones that say "do not trust"
first:

| Banner | Meaning |
|---|---|
| Pannello irraggiungibile | the browser has heard nothing from the panel for two keepalives (10 s): the whole page is greyed and frozen at the last update |
| Daemon fermo | the daemon wrote `stato arresto`. It stays so until a new daemon starts |
| Daemon silenzioso | no line for more than three snapshot periods and at least 1 s (15 s by default; below `--snapshot-ms 334` the 1 s floor applies, because the panel reads the file every 200 ms and checks for silence once a second) |
| Ingresso chiuso | stdin ended, or the file could not be read: nothing more will arrive |
| In attesa della prima riga dal vivo | the page shows what was already in the file, and no new line has arrived yet |
| Stato non garantito | lines were lost; until the next complete snapshot, key holder, PTT and clients can be wrong |
| I fronti arrivano mescolati | edge lines among the status lines read from stdin: see the `2>&1` section above |
| Il daemon parla un vocabolario diverso | the daemon's snapshots carry another vocabulary version: the page shows what it recognises |

## What "guaranteed" means

The status lines are events, and the daemon drops any of them when its
stdout does not keep up; it says so on the next line that gets through
(`stato stdout N righe scartate`). Every `--snapshot-ms` (5 s by default) it
also writes its whole state. The panel trusts the state only from a
complete snapshot, and stops trusting it at the first sign of a loss: a
confession, lost events, a snapshot that did not arrive whole, a line it
cannot read, a new daemon.

So the page can be wrong without saying so only between a loss and the line
that confesses it, which is at most one snapshot period; from the confession
to the next complete snapshot it says "Stato non garantito". The one loss
nobody confesses is the journal's rate limit, and the next snapshot corrects
that one too.

## Tests

```sh
cmake -S host -B host/build && cmake --build host/build
CWNETD=host/build/cwnetd python3 -m unittest discover -s host/panel/tests -t host/panel -v
```

Without `CWNETD` the live tests skip; CI sets it with `PANEL_REQUIRE_LIVE=1`,
which makes a missing daemon a failure. `tests/test_convergence.py` is the
closing test of #86: a recorded stream with lines removed and its start cut
off, through the whole panel, must end on the daemon's real key holder and
the PTT of `--edges`. The recording is `tests/fixtures/session.txt`, made by

```sh
python3 host/panel/tests/record_stream.py --cwnetd host/build/cwnetd
```

Regenerate it when the vocabulary changes: the test fails when the
fixture's vocabulary version is not the panel's.

### The page, by hand

No test runs `panel.js`. What the page shows is decided in
`Model.to_dict()`, where the tests above run, and `tests/test_web.py` reads
the script to check that it only copies those values or looks up their
text. The page computes four things by itself:

- `-` or `?` for a value that is not there;
- event times, in the browser's time zone;
- the "In attesa della prima riga dal vivo" banner before the first
  message from the panel;
- the "Pannello irraggiungibile" banner when `/events` has sent nothing for
  about 10.5 s (`WATCHDOG_MS`: two keepalives and half a second).

Anything else in `panel.js` that chooses what to show is a decision in the
wrong place: it belongs in `Model.to_dict()`, with a test.

The last two have no automatic test. Check them by hand, with the panel
following a daemon, for example the one in
[Loop without the box](../cwnetd/README.md#loop-without-the-box):

1. In the browser's developer tools, block the request to `/events`
   (Network, request blocking) and reload. Only "In attesa della prima riga
   dal vivo" shows, and after about 10.5 s "Pannello irraggiungibile" joins
   it. The block is needed: the panel sends the whole state as soon as the
   page connects, so without it the page's own "no message yet" is never
   on screen.
2. Remove the block and reload: the state appears. Stop the panel. Within
   about 10.5 s "Pannello irraggiungibile" shows and the rest of the page
   greys. Start the panel again: the banner goes and the state comes back.
