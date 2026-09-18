#!/usr/bin/env python3
"""Tests for web.py and cwnetd_panel.py: routes, Host check, caps, /events.

The server runs for real on port 0, in a thread of this process; /events is
read from a raw socket, as a browser would. The two tests that need the
panel as a process (stdin going silent, exit on a signal) start
cwnetd_panel.py itself.

Run from the repository root:
    python3 -m unittest discover -s host/panel/tests -t host/panel -v
"""
import contextlib
import io
import json
import os
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

import cwnetd_panel
import state
import web
from follow import Line
from tests.test_state import snap

PANEL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL_PY = os.path.join(PANEL_DIR, "cwnetd_panel.py")
PANEL_JS = os.path.join(PANEL_DIR, "static", "panel.js")


class FakeClock:
    def __init__(self):
        self.now = 1000.0
        self.lock = threading.Lock()

    def __call__(self):
        with self.lock:
            return self.now

    def advance(self, seconds):
        with self.lock:
            self.now += seconds


def request(port, raw, timeout=5.0):
    """Send raw bytes, read the whole answer (HTTP/1.0: the server closes)."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        s.sendall(raw)
        chunks = []
        while True:
            data = s.recv(65536)
            if not data:
                break
            chunks.append(data)
    return b"".join(chunks)


def get(port, path="/", host=None, method="GET"):
    host = "127.0.0.1:%d" % port if host is None else host
    head = "%s %s HTTP/1.0\r\n" % (method, path)
    if host is not False:
        head += "Host: %s\r\n" % host
    answer = request(port, (head + "\r\n").encode("latin-1"))
    head_b, _sep, body = answer.partition(b"\r\n\r\n")
    lines = head_b.decode("latin-1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = {}
    for line in lines[1:]:
        k, _s, v = line.partition(":")
        headers.setdefault(k.strip().lower(), []).append(v.strip())
    return status, headers, body


class SSE:
    """A stream on /events, read like EventSource does."""

    def __init__(self, port, host=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.sendall(("GET /events HTTP/1.0\r\nHost: %s\r\n\r\n"
                           % (host or "127.0.0.1:%d" % port)).encode())
        self.buf = b""
        head = self._until(b"\r\n\r\n")
        self.status = int(head.split(b" ")[1])

    def _until(self, sep, timeout=5.0):
        self.sock.settimeout(timeout)
        while sep not in self.buf:
            data = self.sock.recv(65536)
            if not data:
                raise EOFError("stream closed")
            self.buf += data
        out, _s, self.buf = self.buf.partition(sep)
        return out

    def next(self, timeout=5.0):
        """(event name, data) of the next dispatched event, within `timeout`
        overall: blocks that dispatch nothing (comments) do not extend it."""
        end = time.monotonic() + timeout
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise socket.timeout("no event in %.1f s" % timeout)
            block = self._until(b"\n\n", remaining).decode("utf-8")
            name, data = "message", []
            for line in block.split("\n"):
                if line.startswith("event: "):
                    name = line[7:]
                elif line.startswith("data: "):
                    data.append(line[6:])
            if data:
                return name, "\n".join(data)

    def state(self, timeout=5.0):
        while True:
            name, data = self.next(timeout)
            if name == "message":
                return json.loads(data)

    def state_until(self, predicate, timeout=5.0):
        end = time.monotonic() + timeout
        while True:
            d = self.state(max(0.1, end - time.monotonic()))
            if predicate(d):
                return d
            if time.monotonic() > end:
                raise AssertionError("condition not met; last state %r" % (d,))

    def close(self):
        self.sock.close()


class ServerTest(unittest.TestCase):
    """A PanelServer on 127.0.0.1, port 0, with a fake clock."""

    server_options = {}
    listen = "127.0.0.1"

    def setUp(self):
        self.clock = FakeClock()
        self.state = web.PanelState(state.Model(), self.clock)
        self.server = web.PanelServer((self.listen, 0), self.state, **self.server_options)
        self.port = self.server.server_address[1]
        t = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05},
                             daemon=True)
        t.start()
        self.addCleanup(self._stop)
        self.streams = []

    def _stop(self):
        for s in self.streams:
            s.close()
        self.state.close()
        self.server.shutdown()
        self.server.server_close()

    def feed(self, *texts):
        self.state.apply_lines([Line(t, False) for t in texts])

    def sse(self, **kw):
        s = SSE(self.port, **kw)
        self.streams.append(s)
        return s

    def wait_for(self, predicate, timeout=5.0):
        end = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > end:
                self.fail("condition not met in %.1f s" % timeout)
            time.sleep(0.01)


class RoutesTest(ServerTest):

    def test_get_root_serves_the_page_with_csp_and_nosniff(self):
        status, headers, body = get(self.port, "/")
        self.assertEqual(status, 200)
        self.assertEqual(headers["content-security-policy"], ["default-src 'self'"])
        self.assertEqual(headers["x-content-type-options"], ["nosniff"])
        self.assertTrue(headers["content-type"][0].startswith("text/html"))
        self.assertIn(b'<script src="/panel.js"', body)

    def test_script_and_style_are_served_with_their_types(self):
        status, headers, body = get(self.port, "/panel.js")
        self.assertEqual(status, 200)
        self.assertTrue(headers["content-type"][0].startswith("text/javascript"))
        self.assertIn(b"EventSource", body)
        status, headers, _body = get(self.port, "/panel.css")
        self.assertEqual(status, 200)
        self.assertTrue(headers["content-type"][0].startswith("text/css"))

    def test_paths_outside_the_routes_never_serve_a_file(self):
        for path in ("/../README.md", "/static/../state.py", "/state.py", "/static/panel.js",
                     "/index.html", "/panel.js?v=1", "/?x=1", "//etc/passwd", "/%2e%2e/web.py"):
            status, headers, body = get(self.port, path)
            self.assertEqual(status, 404, path)
            self.assertNotIn(b"import ", body, path)
            self.assertEqual(headers["content-security-policy"], ["default-src 'self'"], path)

    def test_other_methods_are_refused(self):
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS", "HEAD"):
            status, headers, _body = get(self.port, "/", method=method)
            self.assertEqual(status, 405, method)
            self.assertEqual(headers["allow"], ["GET"], method)


class HostTest(ServerTest):

    def test_loopback_accepts_its_names_with_any_port_and_refuses_the_rest(self):
        p = self.port
        for host, want in [("127.0.0.1:%d" % p, 200), ("LOCALHOST:%d" % p, 200),
                           ("localhost", 200), ("[::1]:%d" % p, 200), ("localhost:8080", 200),
                           ("evil.example", 421), ("evil.example:%d" % p, 421),
                           ("10.0.0.5:%d" % p, 421), ("127.0.0.1:abc", 421),
                           ("127.0.0.1:%d:1" % p, 421), ("", 421), ("[::1", 421)]:
            self.assertEqual(get(p, "/", host=host)[0], want, host)

    def test_missing_or_repeated_host_is_refused(self):
        self.assertEqual(get(self.port, "/", host=False)[0], 421)
        raw = ("GET / HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nHost: localhost\r\n\r\n" % self.port)
        self.assertIn(b" 421 ", request(self.port, raw.encode()).split(b"\r\n")[0])

    def test_refused_host_gets_no_events(self):
        s = self.sse(host="evil.example")
        self.assertEqual(s.status, 421)

    def test_other_address_accepts_literal_ips_and_allowed_names_only(self):
        check = web.HostCheck("192.168.1.10", ["Station.lan"])
        self.assertTrue(check.allowed(["192.168.1.10:7356"]))
        self.assertTrue(check.allowed(["10.8.0.2:7356"]))
        self.assertTrue(check.allowed(["[fd00::2]:7356"]))
        self.assertTrue(check.allowed(["station.LAN:7356"]))
        self.assertFalse(check.allowed(["localhost:7356"]))
        self.assertFalse(check.allowed(["evil.example:7356"]))
        self.assertFalse(check.allowed(["192.168.1.10.evil.example"]))
        self.assertTrue(web.HostCheck("127.0.0.1", ["station.lan"]).allowed(["station.lan"]))

    def test_escape_bytes_and_crlf_in_path_and_host_print_nothing(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            for raw in [b"GET /\x1b[2J HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n",
                        b"GET / HTTP/1.0\r\nHost: \x1b[2Jevil\r\n\r\n",
                        b"GET /a\r\nX-Injected: 1\r\n\r\n",
                        b"GET / HTTP/1.0\r\nHost: 127.0.0.1\r\nX: \x1b]0;t\x07\r\n\r\n",
                        b"\x1b[31m\r\n\r\n"]:
                request(self.port, raw)
            self.wait_for(lambda: self.server.open_connections == 0)
        self.assertEqual(err.getvalue(), "")


class EventsTest(ServerTest):

    server_options = {"keepalive_s": 0.2}

    def test_first_message_is_the_complete_state(self):
        self.feed(*snap(1, [(1, "pronto", "10.0.0.1:50001", 1, 1, "IU3QEZ")], holder="1", ptt=1))
        d = self.sse().state()
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["ptt"])
        self.assertEqual(d["clients"][0]["name"], "IU3QEZ")
        self.assertIn("settings", d)
        self.assertEqual(d["events_max"], state.EVENTS_MAX)

    def test_keepalive_event_arrives_when_nothing_changes(self):
        s = self.sse()
        s.state()
        self.assertEqual(s.next(timeout=2.0), ("keepalive", "1"))

    def test_both_streams_receive_a_level_line_and_a_reader_that_stops_holds_up_nobody(self):
        a, b = self.sse(), self.sse()
        silent = self.sse()  # connected, never read again
        a.state()
        b.state()
        self.feed("stato ptt 1")
        self.assertTrue(a.state_until(lambda d: d["ptt"] is True)["ptt"])
        self.assertTrue(b.state_until(lambda d: d["ptt"] is True)["ptt"])
        started = time.monotonic()
        for i in range(300):
            self.feed("stato latenza client 1 %d ms peak %d ms" % (i, i), "stato ptt %d" % (i % 2))
        self.assertLess(time.monotonic() - started, 1.0, "the reader thread was held up")
        self.feed("stato chiave client 3 LAST")
        self.assertEqual(a.state_until(lambda d: d["key_holder"] == 3)["key_holder_name"], "LAST")
        self.assertIsNotNone(silent)

    def test_client_closing_mid_stream_ends_its_thread_quietly(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            s = SSE(self.port)
            s.state()
            # A reset, not a polite close: the harder case for the writer.
            s.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            s.close()
            for i in range(20):
                self.feed("stato ptt %d" % (i % 2))
                time.sleep(0.02)
            self.wait_for(lambda: self.server.open_streams == 0)
            self.wait_for(lambda: self.server.open_connections == 0)
        self.assertEqual(err.getvalue(), "")

    def test_seventeenth_stream_is_refused_with_503(self):
        for _i in range(web.MAX_STREAMS):
            self.assertEqual(self.sse().status, 200)
        self.wait_for(lambda: self.server.open_streams == web.MAX_STREAMS)
        self.assertEqual(self.sse().status, 503)
        self.assertEqual(get(self.port, "/")[0], 200)

    def test_messages_are_at_most_ten_a_second_and_the_last_one_is_current(self):
        s = self.sse()
        s.state()
        started = time.monotonic()
        stop = started + 1.0
        i = 0
        while time.monotonic() < stop:
            self.feed("stato latenza client 1 %d ms peak %d ms" % (i, i))
            i += 1
            time.sleep(0.002)
        self.feed("stato chiave client 2 END")
        count = 0
        d = None
        while d is None or d["key_holder"] != 2:
            d = s.state()
            count += 1
        elapsed = time.monotonic() - started
        self.assertGreater(i, 100, "the model must change far faster than ten a second")
        self.assertLessEqual(count, elapsed / web.MIN_SEND_INTERVAL_S + 1)

    def test_ae5_callsign_markup_reaches_the_json_as_the_same_string(self):
        name = "<b>X</b>\\x1B"
        self.feed(*snap(1, [(1, "pronto", "10.0.0.1:50001", 1, 1, name)], holder="1"))
        d = self.sse().state()
        self.assertEqual(d["clients"][0]["name"], name)
        self.assertEqual(d["key_holder_name"], name)
        source = open(PANEL_JS, encoding="utf-8").read()
        for sink in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval("):
            self.assertNotIn(sink, source)

    def test_three_banners_together_and_silent_above_the_warnings_in_panel_js(self):
        self.feed(*snap(1, version="v2"), "key 1 123")
        self.clock.advance(16.0)
        self.state.tick()
        d = self.sse().state()
        for banner in ("silent", "mixed_edges", "vocabulary"):
            self.assertIn(banner, d["banners"])
        source = open(PANEL_JS, encoding="utf-8").read()
        order = re.findall(r'"(\w+)"', re.search(r"BANNER_ORDER = \[(.*?)\];", source, re.S).group(1))
        self.assertLess(order.index("silent"), order.index("mixed_edges"))
        self.assertLess(order.index("silent"), order.index("vocabulary"))
        self.assertEqual(order[0], "unreachable")
        self.assertEqual(set(d["banners"]) - set(order), set())


class ConnectionCapTest(ServerTest):

    server_options = {"handler_timeout": 2.0}

    def test_idle_connections_past_the_cap_are_closed_at_once_and_the_rest_time_out(self):
        self.assertEqual(web.HANDLER_TIMEOUT_S, 10.0)
        socks = []
        for _i in range(40):
            s = socket.create_connection(("127.0.0.1", self.port), timeout=5)
            socks.append(s)
            self.addCleanup(s.close)
        # One look at all of them at one instant, well inside the timeout.
        time.sleep(0.3)
        readable, _w, _x = select.select(socks, [], [], 0)
        closed = [s in readable and s.recv(1) == b"" for s in socks]
        self.assertEqual(self.server.open_connections, web.MAX_CONNECTIONS)
        self.assertEqual(closed[:web.MAX_CONNECTIONS], [False] * web.MAX_CONNECTIONS)
        self.assertEqual(closed[web.MAX_CONNECTIONS:], [True] * (40 - web.MAX_CONNECTIONS))
        self.wait_for(lambda: self.server.open_connections == 0, timeout=5.0)
        self.assertEqual(get(self.port, "/")[0], 200)


class PanelProcessTest(unittest.TestCase):

    def start_panel(self, *args, stdin=subprocess.PIPE):
        proc = subprocess.Popen([sys.executable, PANEL_PY, "--port", "0"] + list(args),
                                stdin=stdin, stderr=subprocess.PIPE)
        self.addCleanup(self._kill, proc)
        line = proc.stderr.readline().decode()
        m = re.search(r"http://127\.0\.0\.1:(\d+)/", line)
        self.assertIsNotNone(m, line)
        return proc, int(m.group(1))

    @staticmethod
    def _kill(proc):
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        for f in (proc.stdin, proc.stderr):
            if f is not None:
                f.close()

    def test_shutdown_with_two_open_streams_exits_within_one_second(self):
        proc, port = self.start_panel()
        a, b = SSE(port), SSE(port)
        self.addCleanup(a.close)
        self.addCleanup(b.close)
        a.state()
        b.state()
        started = time.monotonic()
        proc.send_signal(signal.SIGINT)
        proc.wait(timeout=5)
        self.assertLess(time.monotonic() - started, 1.0)
        self.assertEqual(proc.returncode, 0)

    def test_stdin_with_no_lines_for_three_periods_sends_silent_without_a_line(self):
        proc, port = self.start_panel()
        s = SSE(port)
        self.addCleanup(s.close)
        for line in snap(1, period=250):
            proc.stdin.write((line + "\n").encode())
        proc.stdin.flush()
        s.state_until(lambda d: d["liveness"] == "alive" and d["guaranteed"])
        d = s.state_until(lambda d: d["liveness"] == "silent", timeout=4.0)
        self.assertIn("silent", d["banners"])
        proc.stdin.close()
        d = s.state_until(lambda d: d["liveness"] == "input_closed", timeout=4.0)
        self.assertIn("input_closed", d["banners"])


class ReaderFailureTest(unittest.TestCase):

    def test_a_path_that_cannot_be_read_turns_into_input_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                panel = cwnetd_panel.Panel(port=0, follow_path=tmp, tick_s=0.05).start()
                self.addCleanup(panel.stop)
                end = time.monotonic() + 3.0
                while panel.state.current()[1]["liveness"] != "input_closed":
                    self.assertLess(time.monotonic(), end, "the page never said so")
                    time.sleep(0.02)
            self.assertIn("reading stopped", err.getvalue())


if __name__ == "__main__":
    unittest.main()
