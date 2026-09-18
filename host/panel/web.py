#!/usr/bin/env python3
"""HTTP side of the station panel: the page, its two static files and /events.

Read-only by construction (R13): four fixed routes, GET only, the static
files loaded into memory at start-up, nothing taken from the request is ever
used as a path. The page commands nothing and shows callsigns and client
addresses, so it listens on 127.0.0.1 unless told otherwise, and the Host
header is checked against a list, failing closed, so a page elsewhere cannot
reach it through DNS rebinding (KTD12).

/events is Server-Sent Events: every message carries the whole state as
JSON, at most ten a second, and a named "keepalive" event goes out every
KEEPALIVE_S when nothing changes. A named event and not an SSE comment,
because EventSource hides comments from the page, and panel.js needs to see
something arrive to tell a live panel from a dead one.

http.server is not a production server. What it lacks, and is added here
(KTD11): ThreadingHTTPServer opens a thread per connection with no cap, and
BaseHTTPRequestHandler waits for a request without a timeout, so a
connection that never sends one holds a thread for ever. Hence a handler
timeout, which also bounds a blocked write to a stream nobody reads, a cap
on open connections and a cap on streams.
"""
import http.server
import ipaddress
import json
import os
import socket
import threading
import time
from typing import Iterable, Optional

HANDLER_TIMEOUT_S = 10.0
MAX_CONNECTIONS = 32
MAX_STREAMS = 16
KEEPALIVE_S = 5.0
MIN_SEND_INTERVAL_S = 0.1

STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
ROUTES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/panel.js": ("panel.js", "text/javascript; charset=utf-8"),
    "/panel.css": ("panel.css", "text/css; charset=utf-8"),
}
EVENTS_PATH = "/events"

SECURITY_HEADERS = (
    ("Content-Security-Policy", "default-src 'self'"),
    ("X-Content-Type-Options", "nosniff"),
    ("Cache-Control", "no-store"),
)

LOOPBACK_NAMES = ("localhost", "127.0.0.1", "[::1]")


class PanelState:
    """The model, the lock that guards it and the Condition /events waits on.

    The reader thread and the clock thread change the model under the lock;
    a handler takes the state under the lock and writes it to its socket
    after releasing it, so a slow browser never holds up the reader."""

    def __init__(self, model, clock=time.monotonic):
        self._model = model
        self._clock = clock
        self._cond = threading.Condition()
        self._closing = False

    def apply_lines(self, lines) -> None:
        now = self._clock()
        with self._cond:
            for line in lines:
                self._model.apply(line.text, line.backlog, now)
            self._cond.notify_all()

    def input_closed(self) -> None:
        with self._cond:
            self._model.input_closed(self._clock())
            self._cond.notify_all()

    def tick(self) -> None:
        with self._cond:
            before = self._model.version
            self._model.tick(self._clock())
            if self._model.version != before:
                self._cond.notify_all()

    def current(self):
        """(version, the state as a dict), taken together under the lock."""
        with self._cond:
            return self._model.version, self._model.to_dict()

    def wait_for_change(self, version, timeout) -> bool:
        """Until the version moves, the panel closes or `timeout` passes;
        False once the panel is closing."""
        with self._cond:
            self._cond.wait_for(lambda: self._closing or self._model.version != version,
                                timeout)
            return not self._closing

    def pause(self, seconds) -> bool:
        """Sleep that a closing panel cuts short; False once closing."""
        with self._cond:
            self._cond.wait_for(lambda: self._closing, seconds)
            return not self._closing

    @property
    def closing(self) -> bool:
        with self._cond:
            return self._closing

    def close(self) -> None:
        with self._cond:
            self._closing = True
            self._cond.notify_all()


def _host_name(value: str) -> Optional[str]:
    """The name part of a Host header, lower case, or None if malformed."""
    value = value.strip().lower()
    if value.startswith("["):
        end = value.find("]")
        if end < 0:
            return None
        name, rest = value[:end + 1], value[end + 1:]
    elif ":" in value:
        name, _sep, port = value.rpartition(":")
        rest = ":" + port
    else:
        name, rest = value, ""
    if rest and not (rest[0] == ":" and rest[1:].isdigit() and 0 < len(rest) - 1 <= 5):
        return None
    if not name or any(not (ch.isalnum() or ch in ".-:[]") for ch in name):
        return None
    return name


def _is_ip_literal(name: str) -> bool:
    try:
        if name.startswith("[") and name.endswith("]"):
            ipaddress.IPv6Address(name[1:-1])
        else:
            ipaddress.IPv4Address(name)
    except ValueError:
        return False
    return True


def _is_loopback(listen: str) -> bool:
    if listen.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(listen).is_loopback
    except ValueError:
        return False


class HostCheck:
    """Which Host headers the panel answers (KTD12).

    Listening on loopback: localhost, 127.0.0.1 and [::1]. Listening on
    another address: any literal IP, which no DNS rebinding can produce.
    Plus, either way, the names given with --allow-host. The port is not
    compared: a tunnel (ssh -L) changes it, and the name is what a
    rebinding attack would have to fake."""

    def __init__(self, listen: str, allow_hosts: Iterable[str] = ()):
        self.loopback = _is_loopback(listen)
        self.names = {h.strip().lower() for h in allow_hosts if h.strip()}
        if self.loopback:
            self.names.update(LOOPBACK_NAMES)

    def allowed(self, values) -> bool:
        if not values or len(values) != 1:
            return False
        name = _host_name(values[0])
        if name is None:
            return False
        return name in self.names or (not self.loopback and _is_ip_literal(name))


def load_static(directory: str = STATIC_DIR):
    """Every route's body, read once: after this no request touches the disk."""
    out = {}
    for path, (filename, ctype) in ROUTES.items():
        with open(os.path.join(directory, filename), "rb") as fh:
            out[path] = (fh.read(), ctype)
    return out


class PanelHandler(http.server.BaseHTTPRequestHandler):
    """Fixed routes; never SimpleHTTPRequestHandler, which serves a directory."""

    server_version = "cwnetd_panel"
    sys_version = ""
    protocol_version = "HTTP/1.0"

    def setup(self):
        # Read by StreamRequestHandler.setup() to set the socket timeout: it
        # bounds the wait for a request and every write after it.
        self.timeout = self.server.handler_timeout
        self._extra_headers = []
        super().setup()

    def log_message(self, format, *args):
        # Nothing, for every request and for send_error(): the page is not a
        # log, and a request line full of escape bytes must not reach a
        # terminal whatever Python version does the escaping.
        pass

    def end_headers(self):
        for name, value in SECURITY_HEADERS + tuple(self._extra_headers):
            self.send_header(name, value)
        super().end_headers()

    def do_GET(self):
        if not self.server.host_check.allowed(self.headers.get_all("Host")):
            self.send_error(421)
            return
        if self.path == EVENTS_PATH:
            self._events()
            return
        route = self.server.static.get(self.path)
        if route is None:
            self.send_error(404)
            return
        body, ctype = route
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _refuse(self):
        self._extra_headers = [("Allow", "GET")]
        self.send_error(405)

    do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = do_HEAD = _refuse

    def _events(self):
        if not self.server.acquire_stream():
            self.send_error(503)
            return
        try:
            self._stream()
        except OSError:
            # The browser went away, or stopped reading for a whole handler
            # timeout: this stream is over, the others are not affected.
            pass
        finally:
            self.server.release_stream()

    def _send(self, text):
        self.wfile.write(text.encode("utf-8"))

    def _stream(self):
        state = self.server.state
        keepalive_s = self.server.keepalive_s
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.end_headers()
        self._send("retry: 2000\n\n")
        sent_version = None
        next_keepalive = time.monotonic() + keepalive_s
        while not state.closing:
            version, d = state.current()
            if version != sent_version:
                # One line of JSON: json.dumps escapes every newline, so the
                # SSE framing cannot be broken by a callsign.
                self._send("data: %s\n\n" % json.dumps(d, separators=(",", ":")))
                sent_version = version
                next_keepalive = time.monotonic() + keepalive_s
                # At most ten a second: what changes during the pause goes
                # out in the next message, which carries the whole state.
                if not state.pause(MIN_SEND_INTERVAL_S):
                    return
                continue
            remaining = next_keepalive - time.monotonic()
            if remaining <= 0:
                self._send("event: keepalive\ndata: 1\n\n")
                next_keepalive = time.monotonic() + keepalive_s
                continue
            if not state.wait_for_change(sent_version, remaining):
                return


class PanelServer(http.server.ThreadingHTTPServer):
    """ThreadingHTTPServer with a cap on connections and on streams.

    Its threads are daemon threads (daemon_threads = True), so a shutdown
    does not wait for the streams still open."""

    request_queue_size = 64

    def __init__(self, address, state: PanelState, allow_hosts=(),
                 handler_timeout=HANDLER_TIMEOUT_S, keepalive_s=KEEPALIVE_S,
                 max_connections=MAX_CONNECTIONS, max_streams=MAX_STREAMS,
                 static=None):
        self.address_family = socket.AF_INET6 if ":" in address[0] else socket.AF_INET
        self.state = state
        self.host_check = HostCheck(address[0], allow_hosts)
        self.handler_timeout = handler_timeout
        self.keepalive_s = keepalive_s
        self.max_connections = max_connections
        self.max_streams = max_streams
        self.static = static if static is not None else load_static()
        self._count_lock = threading.Lock()
        self._connections = 0
        self._streams = 0
        super().__init__(address, PanelHandler)

    @property
    def open_connections(self) -> int:
        with self._count_lock:
            return self._connections

    @property
    def open_streams(self) -> int:
        with self._count_lock:
            return self._streams

    def process_request(self, request, client_address):
        # Counted on accept, before a thread exists: past the cap the
        # connection is closed at once, and the page is unreachable for at
        # most one handler timeout - the price of a global cap on a page
        # that commands nothing.
        with self._count_lock:
            full = self._connections >= self.max_connections
            if not full:
                self._connections += 1
        if full:
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._release_connection()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._release_connection()

    def _release_connection(self):
        with self._count_lock:
            self._connections -= 1

    def acquire_stream(self) -> bool:
        with self._count_lock:
            if self._streams >= self.max_streams:
                return False
            self._streams += 1
            return True

    def release_stream(self) -> None:
        with self._count_lock:
            self._streams -= 1

    def handle_error(self, request, client_address):
        # A peer that resets the connection is not worth a traceback.
        pass
