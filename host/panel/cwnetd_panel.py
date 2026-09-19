#!/usr/bin/env python3
"""Station panel for cwnetd: a read-only page with who is connected, who holds
the key, the PTT and the daemon's settings, built from its status lines.

The panel reads the lines; it never talks to the daemon, so it can be
started, stopped and restarted at any time without touching the station.

  cwnetd >> /var/log/cwnetd.log &
  python3 cwnetd_panel.py --follow /var/log/cwnetd.log

  journalctl -f -n 0 -o cat -u cwnetd | python3 cwnetd_panel.py

Then open http://127.0.0.1:7356/. By default the page is served only on
this PC; --listen 0.0.0.0 serves it to the LAN or the VPN as well, and
whoever reaches it sees callsigns, client addresses and settings.
See README.md in this directory.
"""
import argparse
import os
import signal
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import follow  # noqa: E402
import state  # noqa: E402
import web  # noqa: E402

DEFAULT_LISTEN = "127.0.0.1"
DEFAULT_PORT = 7356
TICK_S = 1.0
# How often the HTTP loop and main() look for a stop: bounds the exit time.
POLL_S = 0.1


class Panel:
    """Reader thread, clock thread and HTTP server, joined."""

    def __init__(self, listen=DEFAULT_LISTEN, port=DEFAULT_PORT, allow_hosts=(),
                 follow_path=None, stdin=None, clock=time.monotonic, tick_s=TICK_S,
                 **server_options):
        self.follow_path = follow_path
        self.stdin = stdin
        self.tick_s = tick_s
        # Edges in the lines stall the daemon only through a pipe or the
        # journal, which is stdin; a followed file is a regular file.
        self.state = web.PanelState(state.Model(warn_mixed_edges=follow_path is None), clock)
        self.server = web.PanelServer((listen, port), self.state, allow_hosts, **server_options)
        self._stop = threading.Event()
        self._threads = [
            threading.Thread(target=self._read, name="panel-reader", daemon=True),
            threading.Thread(target=self._tick, name="panel-clock", daemon=True),
            threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": POLL_S},
                             name="panel-http", daemon=True),
        ]

    @property
    def port(self):
        return self.server.server_address[1]

    def start(self):
        for t in self._threads:
            t.start()
        return self

    def stop(self):
        self._stop.set()
        self.state.close()
        self.server.shutdown()
        self.server.server_close()

    def _read(self):
        try:
            if self.follow_path is not None:
                follow.follow_file(self.follow_path, self.state.apply_lines, self._stop)
            else:
                stream = self.stdin if self.stdin is not None else sys.stdin.buffer
                follow.read_stream(stream, self.state.apply_lines, self.state.input_closed)
        except Exception as e:  # the page must say the input is gone, not wait for ever
            print("cwnetd_panel: reading stopped: %s" % e, file=sys.stderr)
            self.state.input_closed()

    def _tick(self):
        # Silence is the absence of lines, so it takes a clock of its own:
        # reading stdin, the reader thread sits in read() exactly then.
        while not self._stop.wait(self.tick_s):
            self.state.tick()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--follow", metavar="FILE",
                    help="follow this file by name, like tail -F (without it: stdin)")
    ap.add_argument("--listen", default=DEFAULT_LISTEN,
                    help="address to serve the page on (default %s: this PC only)" % DEFAULT_LISTEN)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help="TCP port of the page (default %d)" % DEFAULT_PORT)
    ap.add_argument("--allow-host", action="append", default=[], metavar="NAME",
                    help="a host name the browser may use to reach the page, "
                         "besides the address itself; repeatable")
    cfg = ap.parse_args(argv)

    try:
        panel = Panel(cfg.listen, cfg.port, cfg.allow_host, follow_path=cfg.follow)
    except OSError as e:
        print("cwnetd_panel: cannot serve on %s:%d: %s" % (cfg.listen, cfg.port, e),
              file=sys.stderr)
        return 1

    stop = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda _signum, _frame: stop.set())
    panel.start()
    host = "[%s]" % cfg.listen if ":" in cfg.listen else cfg.listen
    print("cwnetd_panel: page on http://%s:%d/" % (host, panel.port), file=sys.stderr, flush=True)
    while not stop.wait(POLL_S):
        pass
    panel.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
