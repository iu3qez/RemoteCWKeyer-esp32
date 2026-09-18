#!/usr/bin/env python3
"""Record a real cwnetd session as the fixture of test_convergence.py.

Starts the built cwnetd with --snapshot-ms 250 and --edges to a file, runs a
fixed scenario of cwnet_send.py clients against it, and writes every status
line with the instant it arrived, in ms since the daemon said `stato
ascolto`. The scenario:

  A "IU3QEZ" connects and holds the key down (key_down_only, --hold), then
    drops mid-over: the holder is gone with the key down;
  B "B2 <b>x</b>" (a space and markup in the callsign) connects at the
    start and keys first_over once A has gone;
  C connects while A and B are there and is refused (--max-clients 2);
  SIGINT at the end.

At every snapshot the PTT level is read from --edges, the descriptor that
never drops a line, and written in the header as a checkpoint. If it does
not match the level the snapshot declares, the recording raced and is
refused rather than written.

This is our own recording: it proves the panel against our daemon, not
wire conformance, so the DL4YHF provenance rules of
test_host/cwnet_fixtures.h do not apply to it.

    python3 host/panel/tests/record_stream.py --cwnetd host/build/cwnetd
"""
import argparse
import datetime
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SEND_PY = os.path.join(REPO, "tools", "cwnet", "cwnet_send.py")
DEFAULT_CWNETD = os.path.join(REPO, "host", "build", "cwnetd")
DEFAULT_OUT = os.path.join(HERE, "fixtures", "session.txt")
SNAPSHOT_MS = 250

# (seconds after `stato ascolto`, cwnet_send.py arguments)
SCENARIO = [
    (0.3, ["--call", "IU3QEZ", "--fixture", "key_down_only", "--delay", "0.8", "--hold", "3.0"]),
    (0.5, ["--call", "B2 <b>x</b>", "--fixture", "first_over", "--delay", "3.6", "--hold", "5.5"]),
    (1.6, ["--call", "C3", "--fixture", "first_over", "--hold", "1.0"]),
]
STOP_AT_S = 7.0

SNAP_OPEN = re.compile(r"stato snap inizio (v\d+) istanza \S+ seq (\d+) client \d+ chiave \S+ ptt ([01])")
SNAP_CLOSE = re.compile(r"stato snap fine seq (\d+)")


def last_ptt(edges_path):
    level = 0
    with open(edges_path) as fh:
        for line in fh:
            if line.startswith("ptt "):
                level = int(line.split()[1])
    return level


def record(cwnetd, out_path):
    tmp = tempfile.mkdtemp(prefix="cwnetd-rec-")
    args = [cwnetd, "--listen", "127.0.0.1", "--port", "0", "--max-clients", "2",
            "--snapshot-ms", str(SNAPSHOT_MS), "--edges", "edges.log"]
    # Run in the temporary directory so the `stato uscita` line names
    # edges.log, not a path of this machine.
    proc = subprocess.Popen(args, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    lines = []
    checkpoints = []
    raced = []
    ready = threading.Event()
    port = []
    t0 = [None]
    snap_ptt = {}

    def reader():
        for raw in proc.stdout:
            now = time.monotonic()
            text = raw.decode("ascii", "replace").rstrip("\n")
            if t0[0] is None:
                t0[0] = now
            lines.append((int(round((now - t0[0]) * 1000)), text))
            m = SNAP_OPEN.match(text)
            if m:
                snap_ptt[int(m.group(2))] = int(m.group(3))
            m = SNAP_CLOSE.fullmatch(text)
            if m:
                seq = int(m.group(1))
                edges = last_ptt(os.path.join(tmp, "edges.log"))
                checkpoints.append((seq, edges))
                if snap_ptt.get(seq) != edges:
                    raced.append(seq)
            if text.startswith("stato ascolto "):
                port.append(int(text.split()[2].rsplit(":", 1)[1]))
                ready.set()

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    if not ready.wait(5):
        proc.kill()
        raise SystemExit("cwnetd did not say `stato ascolto`")
    start = time.monotonic()
    clients = []
    for at_s, client_args in SCENARIO:
        time.sleep(max(0.0, start + at_s - time.monotonic()))
        clients.append(subprocess.Popen(
            [sys.executable, SEND_PY, "--host", "127.0.0.1", "--port", str(port[0])] + client_args,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(max(0.0, start + STOP_AT_S - time.monotonic()))
    proc.send_signal(signal.SIGINT)
    proc.wait(timeout=5)
    t.join(timeout=5)
    for c in clients:
        c.wait(timeout=10)
    if raced:
        raise SystemExit("snapshot and --edges disagree at seq %s: the recording raced, run it again"
                         % raced)

    commit = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    vocab = next(SNAP_OPEN.match(t).group(1) for _ms, t in lines if SNAP_OPEN.match(t))
    header = [
        "# cwnetd status lines recorded by host/panel/tests/record_stream.py, each",
        "# with its arrival time in ms after `stato ascolto`. Our own recording: it",
        "# proves the panel against our daemon, not wire conformance.",
        "# command: %s" % " ".join(os.path.basename(a) if i == 0 else a for i, a in enumerate(args)),
        "# commit: %s" % commit,
        "# date: %s" % datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "# snapshot-ms: %d" % SNAPSHOT_MS,
        "# vocabulary: %s" % vocab,
    ]
    for at_s, client_args in SCENARIO:
        header.append("# client at %.1f s: cwnet_send.py %s" % (at_s, " ".join(
            "'%s'" % a if " " in a else a for a in client_args)))
    header.append("# SIGINT at %.1f s" % STOP_AT_S)
    header.append("# checkpoint: the PTT level of --edges when each snapshot was read")
    for seq, level in checkpoints:
        header.append("# checkpoint seq=%d edges-ptt=%d" % (seq, level))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as fh:
        fh.write("\n".join(header) + "\n")
        for ms, text in lines:
            fh.write("%d %s\n" % (ms, text))
    return len(lines), len(checkpoints)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cwnetd", default=DEFAULT_CWNETD)
    ap.add_argument("--out", default=DEFAULT_OUT)
    cfg = ap.parse_args()
    n, k = record(os.path.abspath(cfg.cwnetd), cfg.out)
    print("%d lines, %d checkpoints -> %s" % (n, k, cfg.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
