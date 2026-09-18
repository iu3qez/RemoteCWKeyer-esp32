#!/usr/bin/env python3
"""The panel against the running daemon: the built cwnetd named by $CWNETD,
cwnet_send.py clients, --snapshot-ms 250 and --edges to a file.

The expected PTT is the last `ptt` line of the --edges file, which never
drops one; the expected key holder is the client the test makes key. The
state is checked, not the timing, except where the plan names a limit.

Without $CWNETD these tests skip; with PANEL_REQUIRE_LIVE=1, as in CI, a
missing $CWNETD is a failure instead, so the live run cannot quietly stop
running.

    CWNETD=host/build/cwnetd python3 -m unittest discover -s host/panel/tests -t host/panel
"""
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest

import cwnetd_panel
import follow
import web
from tests.test_web import SSE

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SEND_PY = os.path.join(REPO, "tools", "cwnet", "cwnet_send.py")
CWNETD = os.environ.get("CWNETD")
REQUIRE_LIVE = os.environ.get("PANEL_REQUIRE_LIVE") == "1"

SNAPSHOT_MS = 250
# A loaded CI runner can be late by this much on top of the chain.
CI_MARGIN_S = 1.0
# KTD13: the sum of the chain, not a number of periods - two snapshot
# periods, the file polling, the minimum interval between two /events
# messages, and the margin.
CONVERGENCE_LIMIT_S = (2 * SNAPSHOT_MS / 1000.0 + follow.POLL_INTERVAL_S
                       + web.MIN_SEND_INTERVAL_S + CI_MARGIN_S)

# The `long` fixture spans about 7.5 s of keying in one burst; the holder
# sends nothing else, so --idle must outlast it (as in cwnet_jitter.py).
LONG_IDLE_MS = 13000


class LiveTest(unittest.TestCase):

    def setUp(self):
        if not CWNETD:
            if REQUIRE_LIVE:
                self.fail("PANEL_REQUIRE_LIVE=1 and no $CWNETD: the live run did not run")
            self.skipTest("set CWNETD to the built cwnetd to run the live tests")
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = tmp.name
        self.status = os.path.join(self.dir, "status.log")
        self.edges = os.path.join(self.dir, "edges.log")
        self.relayed = []

    # --- the station -----------------------------------------------------

    def start_daemon(self, *extra, gate=None):
        """cwnetd with its stdout appended to the followed file, as the README
        says to run it; or, with `gate`, through a relay that starts copying
        after the first line for which gate(line) is true, that line
        excluded."""
        args = [os.path.abspath(CWNETD), "--listen", "127.0.0.1", "--port", "0",
                "--snapshot-ms", str(SNAPSHOT_MS), "--edges", self.edges] + list(extra)
        if gate is None:
            out = open(self.status, "ab")
            proc = subprocess.Popen(args, stdout=out, stderr=subprocess.DEVNULL)
            out.close()
        else:
            proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            self.gate_open = threading.Event()
            threading.Thread(target=self._relay, args=(proc, gate), daemon=True).start()
        self.addCleanup(self._stop, proc)
        self.port = self._wait_port()
        return proc

    def _relay(self, proc, gate):
        with open(self.status, "ab", buffering=0) as out:
            for raw in proc.stdout:
                line = raw.decode("ascii", "replace").rstrip("\n")
                self.relayed.append(line)
                if self.gate_open.is_set():
                    out.write(raw)
                elif gate(line):
                    self.gate_open.set()

    def _wait_port(self):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            for line in self._lines():
                m = re.match(r"stato ascolto 127\.0\.0\.1:(\d+) ", line)
                if m:
                    return int(m.group(1))
            time.sleep(0.02)
        self.fail("cwnetd did not say `stato ascolto`")

    def _lines(self):
        if self.relayed:
            return list(self.relayed)
        try:
            with open(self.status) as fh:
                return fh.read().splitlines()
        except FileNotFoundError:
            return []

    @staticmethod
    def _stop(proc):
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        if proc.stdout is not None:
            proc.stdout.close()

    def client(self, fixture, call, hold, delay=0.2):
        p = subprocess.Popen([sys.executable, SEND_PY, "--host", "127.0.0.1", "--port",
                              str(self.port), "--fixture", fixture, "--call", call,
                              "--hold", str(hold), "--delay", str(delay)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def reap():
            if p.poll() is None:
                p.kill()
            p.wait()
        self.addCleanup(reap)
        return p

    def index_of(self, call, timeout=5.0):
        """The client index the daemon gave `call`."""
        end = time.monotonic() + timeout
        pattern = re.compile(r"stato connesso client (\d+) %s da " % re.escape(call))
        while time.monotonic() < end:
            for line in self._lines():
                m = pattern.match(line)
                if m:
                    return int(m.group(1))
            time.sleep(0.02)
        self.fail("%s never connected" % call)

    def edges_ptt(self):
        """The PTT level on --edges now: its last `ptt` line."""
        level = False
        with open(self.edges) as fh:
            for line in fh:
                if line.startswith("ptt "):
                    level = line.split()[1] == "1"
        return level

    def panel(self):
        p = cwnetd_panel.Panel(port=0, follow_path=self.status).start()
        self.addCleanup(p.stop)
        sse = SSE(p.port)
        self.addCleanup(sse.close)
        return sse

    # --- the tests -------------------------------------------------------

    def test_key_held_down_shows_the_holder_and_the_edges_ptt_then_ptt_off_after_the_tail(self):
        self.start_daemon()
        sse = self.panel()
        self.client("key_down_only", "LIVE1", hold=2.0)
        idx = self.index_of("LIVE1")
        d = sse.state_until(lambda d: d["guaranteed"] and d["key_holder"] == idx
                            and d["ptt"] is True, timeout=5)
        self.assertEqual(d["key_holder_name"], "LIVE1")
        self.assertTrue(self.edges_ptt(), "--edges must agree: PTT on")
        d = sse.state_until(lambda d: d["key_known"] and d["key_holder"] is None
                            and d["ptt"] is False, timeout=8)
        self.assertTrue(d["guaranteed"])
        self.assertFalse(self.edges_ptt(), "--edges must agree: PTT off after the tail")

    def test_panel_started_mid_over_converges_within_the_chain_limit(self):
        # Nothing up to and including the PTT going on reaches the file: the
        # panel never reads the key line or a PTT line, only what follows.
        self.start_daemon(gate=lambda line: line == "stato ptt 1")
        self.client("key_down_only", "LIVE2", hold=5.0)
        self.assertTrue(self.gate_open.wait(5), "the key was never taken")
        idx = self.index_of("LIVE2")
        started = time.monotonic()
        sse = self.panel()
        d = sse.state_until(lambda d: d["guaranteed"] and d["key_holder"] == idx
                            and d["ptt"] is True, timeout=CONVERGENCE_LIMIT_S + 5)
        elapsed = time.monotonic() - started
        self.assertLessEqual(elapsed, CONVERGENCE_LIMIT_S)
        self.assertTrue(self.edges_ptt())
        with open(self.status) as fh:
            seen = fh.read()
        self.assertNotIn("stato chiave", seen)
        self.assertNotIn("stato ptt", seen)
        self.assertEqual(d["key_holder_name"], "LIVE2")

    def test_ae6_long_over_keeps_one_holder_and_the_guarantee_while_ptt_alternates(self):
        self.start_daemon("--idle", str(LONG_IDLE_MS))
        sse = self.panel()
        sse.state_until(lambda d: d["guaranteed"] and d["liveness"] == "alive", timeout=5)
        self.client("long", "LIVE6", hold=11.0)
        idx = self.index_of("LIVE6")
        d = sse.state_until(lambda d: d["key_holder"] == idx, timeout=5)
        first = time.monotonic()
        during = [d]
        while True:
            d = sse.state(timeout=15)
            if d["key_holder"] != idx:
                break
            during.append(d)
        span = time.monotonic() - first
        self.assertIsNone(d["key_holder"], "the over must end with the key freed")
        self.assertGreater(span, 7.0, "the messages must cover the whole over")
        for m in during:
            self.assertTrue(m["guaranteed"], "not guaranteed at version %d" % m["version"])
            self.assertEqual(m["liveness"], "alive")
        lines = self._lines()
        start = lines.index("stato chiave client %d LIVE6" % idx)
        ptt_lines = [ln for ln in lines[start:] if ln.startswith("stato ptt ")]
        self.assertGreaterEqual(len(ptt_lines), 10, "the PTT must alternate during the over")


if __name__ == "__main__":
    unittest.main()
