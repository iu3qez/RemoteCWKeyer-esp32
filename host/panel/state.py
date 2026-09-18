#!/usr/bin/env python3
"""The station panel's model: cwnetd status lines in, the state the page shows out.

Pure: no I/O, no threads, no clock of its own. The caller hands every line
with the instant it arrived and whether it was already in the file when the
panel opened it (backlog), and ticks the model so that silence is noticed
even when no line arrives. The contract for the lines is the vocabulary in
host/cwnetd/README.md, "Reading a status line": a line those tables do not
describe is not a line this module reads.

What is true, and when
----------------------
The status lines are events, and the daemon drops any of them when stdout
does not keep up. So the model keeps two things apart:

- the state: clients, key holder, PTT, settings. Level lines move it, and a
  complete snapshot (several lines tied by a sequence number) replaces it;
- the guarantee: the state is only guaranteed from a complete snapshot until
  the first sign that a line was lost - a confession of dropped lines, lost
  events, a discarded snapshot, an unreadable line, a new daemon.

Liveness (waiting, alive, silent, stopped, input closed) is a third thing: a
state can be guaranteed and the daemon silent. The page is certain only when
the daemon is alive and the state guaranteed.

A short write to a socket (journald) lets half a line out, counts the line
as dropped, and glues the confession that follows to the half: the split in
parse() is for that. The declared name length in a snapshot client line is
what lets a name containing "stato stdout" survive it.
"""
import collections
import re
import time
from typing import Callable, Dict, List, NamedTuple

# The vocabulary this module reads (README, "The periodic snapshot").
VOCABULARY = "v1"

# CWNETD_LINE_MAX is 256: minus the newline and vsnprintf()'s NUL, a status
# line carries at most 254 characters, and a longer one is cut there.
LINE_MAX = 254

# The page shows this many events, and says so.
EVENTS_MAX = 50

# The daemon's default --snapshot-ms, used until a snapshot says otherwise.
DEFAULT_PERIOD_MS = 5000

# No line for more than this many periods: the daemon is silent.
SILENT_PERIODS = 3

SETTINGS = ("max_clients", "play_floor_ms", "link_ceiling_ms", "ptt_tail_ms", "ptt_lead_ms",
            "idle_ms", "over_max_ms", "handshake_ms", "out_cap_bytes")


class Parsed(NamedTuple):
    kind: str
    fields: dict
    text: str


_ADDR = r"(?:\d{1,3}(?:\.\d{1,3}){3}:\d{1,5}|\?)"

# Fields kept as text; every other field is a number.
_TEXT_FIELDS = {"listen", "dest", "error", "backend", "addr", "name", "reason", "vocabulary",
                "instance", "holder", "state"}

# Recognised whole, never by prefix, in this order: the "uscita fronti (...)"
# lines before "uscita BACKEND fronti DEST", and the shutdown total is its own
# kind rather than a confession. Where a name sits in the middle, the pattern
# is anchored on what follows it, because a name can hold " da ", ":" and "#".
_GRAMMAR = [(kind, re.compile(rx.replace("ADDR", _ADDR), re.DOTALL)) for kind, rx in [
    ("ascolto", r"stato ascolto (?P<listen>\S+:\d+) max-clients (?P<max_clients>\d+) "
                r"B>=(?P<play_floor_ms>\d+) ms tetto (?P<link_ceiling_ms>\d+) ms "
                r"coda (?P<ptt_tail_ms>\d+) ms lead (?P<ptt_lead_ms>\d+) ms "
                r"out-cap (?P<out_cap_bytes>\d+) byte"),
    ("edges_stall", r"stato uscita fronti \((?P<dest>.*)\) non drena: (?P<ms>\d+) ms e aspetto"),
    ("edges_refused", r"stato uscita fronti \((?P<dest>.*)\) rifiuta: (?P<error>.*)"),
    ("edges_summary", r"stato uscita fronti \((?P<dest>.*)\): (?P<waits>\d+) attese per "
                      r"(?P<ms>-?\d+) ms, (?P<errors>\d+) errori, (?P<lost>\d+) persi"),
    ("uscita", r"stato uscita (?P<backend>\S+) fronti (?P<dest>.+)"),
    ("accepted", r"stato accettato client (?P<idx>\d+) da (?P<addr>ADDR)"),
    ("refused", r"stato rifiutato da (?P<addr>ADDR): nessuno slot libero"),
    ("connected", r"stato connesso client (?P<idx>\d+) (?P<name>.*) da (?P<addr>ADDR)"),
    ("disconnected", r"stato disconnesso client (?P<idx>\d+) (?P<name>.*) da (?P<addr>ADDR): "
                     r"(?P<reason>.+)"),
    ("reader_stalled", r"stato client (?P<idx>\d+) lettore fermo: (?P<bytes>\d+) byte non "
                       r"inviati, chiudo"),
    ("slot_in_use", r"stato slot client (?P<idx>\d+) ancora in uso da (?P<addr>ADDR): chiudo "
                    r"la connessione precedente"),
    ("accept_failed", r"stato accept fallita: (?P<error>.*)"),
    ("key_free", r"stato chiave libera"),
    ("key", r"stato chiave client (?P<idx>\d+) (?P<name>.*)"),
    ("ptt", r"stato ptt (?P<level>[01])"),
    ("latency", r"stato latenza client (?P<idx>\d+) (?P<lat>-?\d+) ms peak (?P<peak>-?\d+) ms"),
    ("unfit", r"stato link non idoneo client (?P<idx>\d+) (?P<name>.*): peak (?P<peak>-?\d+) ms"),
    ("over", r"stato over client (?P<idx>\d+) (?P<name>.*) B (?P<b>\d+) ms"),
    ("late_bytes", r"stato byte in ritardo client (?P<idx>\d+) (?P<name>.*): (?P<bytes>\d+) "
                   r"byte, (?P<ms>-?\d+) ms in totale"),
    # The reasons carry no ':', so the last ": " is where the name ends.
    ("fault", r"stato fault client (?P<idx>\d+) (?P<name>.*): (?P<reason>[^:]*)"),
    ("fault", r"stato fault: (?P<reason>.*)"),
    ("lost_events", r"stato eventi persi (?P<n>\d+)"),
    ("poll_failed", r"stato poll fallita: (?P<error>.*)"),
    ("stopped", r"stato arresto"),
    ("confession", r"stato stdout (?P<n>\d+) righe scartate"),
    ("dropped_total", r"stato stdout (?P<n>\d+) righe scartate in totale"),
    ("snap_open", r"stato snap inizio (?P<vocabulary>v\d+) istanza (?P<instance>\d+-\d+) "
                  r"seq (?P<seq>\d+) client (?P<k>\d+) chiave (?P<holder>libera|\d+) "
                  r"ptt (?P<ptt>[01]) periodo (?P<period_ms>\d+)"),
    ("snap_settings", r"stato snap manopole max-clients (?P<max_clients>\d+) "
                      r"B>= (?P<play_floor_ms>\d+) tetto (?P<link_ceiling_ms>\d+) "
                      r"coda (?P<ptt_tail_ms>\d+) lead (?P<ptt_lead_ms>\d+) "
                      r"idle (?P<idle_ms>\d+) over-max (?P<over_max_ms>\d+) "
                      r"handshake (?P<handshake_ms>\d+) out-cap (?P<out_cap_bytes>\d+)"),
    ("snap_close", r"stato snap fine seq (?P<seq>\d+)"),
]]

# The name last, preceded by its length: the space before an empty name is
# optional, so a reader that trims lines does not make it unreadable.
_SNAP_CLIENT = re.compile(
    r"stato snap client (?P<pos>\d+)/(?P<k>\d+) (?P<idx>\d+) (?P<state>pronto|attesa) "
    r"(?P<addr>" + _ADDR + r") lat (?P<lat>-?\d+) peak (?P<peak>-?\d+) "
    r"nome (?P<length>\d+)(?: (?P<name>.*))?", re.DOTALL)

_CONFESSION_AT = "stato stdout "
_CONFESSION = _GRAMMAR[[k for k, _ in _GRAMMAR].index("confession")][1]
_EDGE = re.compile(r"(?:key|ptt) [01] -?\d+")
_WORD = re.compile(r"stato ([^\s:]+)")

# First words of the lines the README describes. A "stato" line starting
# with one of these that matches no pattern was damaged on its way: it is
# unreadable. Any other first word is a line this vocabulary does not know.
_KNOWN_WORDS = {"ascolto", "uscita", "accettato", "rifiutato", "connesso", "disconnesso",
                "client", "slot", "accept", "chiave", "ptt", "latenza", "link", "over", "byte",
                "fault", "eventi", "poll", "arresto", "stdout", "snap"}


def _fields(groups):
    out = {}
    for key, value in groups.items():
        if value is not None and key not in _TEXT_FIELDS:
            value = int(value)
        out[key] = value
    return out


def _parse_snap_client(text):
    m = _SNAP_CLIENT.fullmatch(text)
    if m is None:
        return None
    fields = _fields(m.groupdict())
    name = fields["name"] or ""
    declared = fields.pop("length")
    if len(name) == declared:
        truncated = False
    elif len(name) < declared and len(text) == LINE_MAX:
        # Cut by the daemon's line cap. Shorter than declared on a line
        # below the cap is something else: half a line with a confession
        # glued after it, left to the split below.
        truncated = True
    else:
        return None
    fields["name"] = name
    fields["name_truncated"] = truncated
    return Parsed("snap_client", fields, text)


def _parse_whole(text):
    if not text.startswith("stato "):
        # Edges merged into the stream (2>&1) and the daemon's own startup
        # messages. An edge still says something: see Model, mixed_edges.
        return Parsed("edge" if _EDGE.fullmatch(text) else "ignored", {}, text)
    for kind, rx in _GRAMMAR:
        m = rx.fullmatch(text)
        if m is not None:
            return Parsed(kind, _fields(m.groupdict()), text)
    word = _WORD.match(text)
    if word is not None and word.group(1) in _KNOWN_WORDS:
        return Parsed("unreadable", {}, text)
    return Parsed("unknown", {}, text)


def parse(text: str) -> List[Parsed]:
    """One status line in, what it says out: usually one item, two when a
    confession was glued to half a line."""
    if text.startswith("stato snap client "):
        p = _parse_snap_client(text)
        if p is not None:
            return [p]
    cut = text.rfind(_CONFESSION_AT)
    if cut > 0:
        # The half before is unreadable whatever it looks like: "stato chiave
        # client 1 IU3" parses, and names the wrong callsign. The part after
        # is read only as a complete confession, the one thing status_line()
        # writes after a failed write; any other tail would let a callsign
        # like "x stato arresto" say the daemon stopped. The last occurrence,
        # because the confession is what ends the line.
        head, tail = text[:cut], text[cut:]
        m = _CONFESSION.fullmatch(tail)
        if m is not None:
            tail_p = Parsed("confession", {"n": int(m.group("n")), "glued": True}, tail)
        else:
            tail_p = Parsed("unreadable", {}, tail)
        return [Parsed("unreadable", {}, head), tail_p]
    return [_parse_whole(text)]


# Kinds that only go to the event list, and the three of them that carry the
# edges path, which the page never shows (KTD7).
_EVENT_ONLY = {"refused", "reader_stalled", "slot_in_use", "accept_failed", "unfit", "over",
               "late_bytes", "fault", "poll_failed", "dropped_total"}
_EDGES_EVENTS = {"edges_stall", "edges_refused", "edges_summary"}


def _new_client(idx, addr):
    return {"idx": idx, "addr": addr, "name": "", "name_truncated": False, "ready": False,
            "lat": -1, "peak": -1}


def _body(text):
    return text.removeprefix("stato ")


class Model:
    """The state the page shows, built from the lines. Not thread-safe: the
    caller holds a lock around every call."""

    def __init__(self, wall: Callable[[], float] = time.time):
        self._wall = wall
        # Grows at every change the page must see; /events sends on a change.
        self.version = 0
        self.events = collections.deque(maxlen=EVENTS_MAX)
        # Survive a daemon restart: they describe the input, not the daemon.
        self.mixed_edges = False
        self._eof = False
        self._last_live = None
        self.liveness = "waiting"
        self._reset_instance()

    # --- input -----------------------------------------------------------

    def apply(self, text: str, backlog: bool, now: float) -> None:
        """One line, as it arrived at `now` (monotonic seconds). Backlog lines
        count for the state, not for liveness."""
        for p in parse(text):
            self._apply(p)
        if not backlog:
            self._last_live = now
        self._update_liveness(now)
        self.version += 1

    def tick(self, now: float) -> None:
        """Liveness moves with time too: silence is the absence of lines."""
        if self._update_liveness(now):
            self.version += 1

    def input_closed(self, now: float) -> None:
        if not self._eof:
            self._eof = True
            self._update_liveness(now)
            self.version += 1

    # --- output ----------------------------------------------------------

    def to_dict(self) -> Dict:
        """Everything the page shows, JSON-ready: the only format sent."""
        ceiling = self.settings["link_ceiling_ms"]
        clients = []
        for idx in sorted(self.clients):
            c = self.clients[idx]
            peak = c["peak"] if c["peak"] >= 0 else None
            clients.append({
                "idx": idx,
                "addr": c["addr"],
                "name": c["name"],
                "name_truncated": c["name_truncated"],
                "ready": c["ready"],
                "lat_ms": c["lat"] if c["lat"] >= 0 else None,
                "peak_ms": peak,
                # KTD4: the core does not expose "unfit"; the peak over the
                # ceiling is what makes it so.
                "unfit": peak is not None and ceiling is not None and peak > ceiling,
            })
        return {
            "vocabulary": VOCABULARY,
            "version": self.version,
            "liveness": self.liveness,
            "guaranteed": self.guaranteed,
            "certain": self.liveness == "alive" and self.guaranteed,
            "banners": self._banners(),
            "daemon_vocabulary": self.daemon_vocabulary,
            "instance": self.instance,
            "period_ms": self.period_ms,
            "listen": self.listen,
            "output": dict(self.output),
            "key_known": self.key_known,
            "key_holder": self.key_holder,
            "key_holder_name": self.key_holder_name,
            "ptt": self.ptt,
            "settings": dict(self.settings),
            "clients": clients,
            "events": list(self.events),
            "events_max": EVENTS_MAX,
        }

    # --- the rules -------------------------------------------------------

    def _reset_instance(self):
        """What belongs to one daemon process, back to unknown."""
        self.instance = None
        self.stopped = False
        self.guaranteed = False
        self.clients = {}
        self.key_known = False
        self.key_holder = None
        self.key_holder_name = None
        self.ptt = None
        self.period_ms = DEFAULT_PERIOD_MS
        self.listen = None
        self.output = {"backend": None, "edges": None}
        self.settings = dict.fromkeys(SETTINGS)
        self.daemon_vocabulary = None
        self._confessed = 0
        self._last_seq = None
        self._snap = None

    def _event(self, kind, text):
        self.events.append({"t": self._wall(), "kind": kind, "text": text})

    def _not_guaranteed(self):
        self.guaranteed = False

    def _banners(self):
        # KTD9: the ones that say "do not trust" first, in this order, then
        # the configuration warnings. They stack; none hides another.
        out = []
        if self.liveness != "alive":
            out.append(self.liveness)
        if not self.guaranteed:
            out.append("not_guaranteed")
        if self.mixed_edges:
            out.append("mixed_edges")
        if self.daemon_vocabulary not in (None, VOCABULARY):
            out.append("vocabulary")
        return out

    def _update_liveness(self, now):
        if self.stopped:
            lv = "stopped"
        elif self._eof:
            lv = "input_closed"
        elif self._last_live is None:
            lv = "waiting"
        elif now - self._last_live > SILENT_PERIODS * self.period_ms / 1000.0:
            lv = "silent"
        else:
            lv = "alive"
        changed = lv != self.liveness
        self.liveness = lv
        return changed

    def _apply(self, p):
        if self._snap is not None and not p.kind.startswith("snap_"):
            # Nothing is written between the lines of one snapshot: a line
            # in the middle means some of it did not arrive as written.
            self._discard_snapshot()
        if p.kind in _EVENT_ONLY:
            # KTD7: these say what happened, not what is. A fault in
            # particular says nothing about who holds the key.
            self._event(p.kind, _body(p.text))
        elif p.kind in _EDGES_EVENTS:
            where = "stderr" if p.fields["dest"] == "stderr" else "file"
            self._event(p.kind, _body(p.text).replace("(%s)" % p.fields["dest"],
                                                      "(%s)" % where, 1))
        else:
            getattr(self, "_on_" + p.kind)(p.fields, p.text)

    # Level lines: they move the state.

    def _on_ascolto(self, f, text):
        self._reset_instance()
        self._event("new_instance", _body(text))
        self.listen = f["listen"]
        # The ascolto pattern names its groups after SETTINGS: it carries six of
        # the nine, and the snapshot brings the rest.
        for key in SETTINGS:
            if key in f:
                self.settings[key] = f[key]

    def _on_uscita(self, f, text):
        # KTD7: whether the edges go to a file, never the path, which on a
        # LAN page would show user names and directories.
        self.output = {"backend": f["backend"],
                       "edges": "stderr" if f["dest"] == "stderr" else "file"}

    def _on_accepted(self, f, text):
        c = self.clients.get(f["idx"])
        if c is None or c["addr"] != f["addr"]:
            # A client is the pair index and address.
            self.clients[f["idx"]] = _new_client(f["idx"], f["addr"])

    def _on_connected(self, f, text):
        c = self.clients.get(f["idx"])
        if c is None or c["addr"] != f["addr"]:
            c = _new_client(f["idx"], f["addr"])
            self.clients[f["idx"]] = c
        c["ready"] = True
        c["name"] = f["name"]
        c["name_truncated"] = False

    def _on_disconnected(self, f, text):
        c = self.clients.get(f["idx"])
        if c is not None and c["addr"] == f["addr"]:
            del self.clients[f["idx"]]
        self._event("disconnected", _body(text))

    def _on_key_free(self, f, text):
        self.key_known = True
        self.key_holder = None
        self.key_holder_name = None

    def _on_key(self, f, text):
        self.key_known = True
        self.key_holder = f["idx"]
        self.key_holder_name = f["name"]

    def _on_ptt(self, f, text):
        self.ptt = f["level"] == 1

    def _on_latency(self, f, text):
        c = self.clients.get(f["idx"])
        if c is not None:
            c["lat"] = f["lat"]
            c["peak"] = f["peak"]

    def _on_stopped(self, f, text):
        # Left only by a new instance: what the daemon writes after this
        # line is its shutdown, applied without leaving "stopped".
        self.stopped = True
        self._event("stopped", _body(text))

    # Lines that say something was lost.

    def _on_lost_events(self, f, text):
        self._not_guaranteed()
        self._event("lost_events", _body(text))

    def _on_confession(self, f, text):
        if f.get("glued"):
            # Always a loss: the half line before it. Its count is not
            # trusted either, because a callsign can carry this text, and
            # a huge count taken from one would silence every later
            # confession. The daemon's next one is larger anyway.
            self._not_guaranteed()
            self._event("dropped_lines", _body(text))
            return
        if f["n"] > self._confessed:
            self._confessed = f["n"]
            self._not_guaranteed()
            self._event("dropped_lines", _body(text))

    def _on_unreadable(self, f, text):
        self._not_guaranteed()
        if text.startswith("stato uscita"):
            self._event("unreadable", "riga illeggibile (testo omesso: puo' contenere un percorso)")
        else:
            self._event("unreadable", "riga illeggibile: " + _body(text))

    def _on_unknown(self, f, text):
        self._event("unknown", _body(text))

    def _on_edge(self, f, text):
        # KTD6: edges mixed into the status mean stderr shares the pipe or
        # the journal with stdout, and a slow panel can then stall the
        # daemon's edge_write() - the station with it.
        self.mixed_edges = True

    def _on_ignored(self, f, text):
        pass

    # The snapshot: accumulated, applied whole or not at all.

    def _discard_snapshot(self):
        self._snap = None
        self._not_guaranteed()
        self._event("discarded_snapshot", "istantanea incompleta scartata")

    def _stray_snapshot_line(self):
        # Its opening never arrived: some lines were lost, confessed or not
        # (journald's rate limit drops without a confession).
        self._not_guaranteed()

    def _on_snap_open(self, f, text):
        if self._snap is not None:
            self._discard_snapshot()
        self._snap = {"open": f, "settings": None, "clients": []}

    def _on_snap_settings(self, f, text):
        if self._snap is None:
            self._stray_snapshot_line()
        elif self._snap["settings"] is not None or self._snap["clients"]:
            self._discard_snapshot()
        else:
            self._snap["settings"] = f

    def _on_snap_client(self, f, text):
        s = self._snap
        if s is None:
            self._stray_snapshot_line()
        elif (s["settings"] is None or f["k"] != s["open"]["k"] or
              f["pos"] != len(s["clients"]) + 1):
            self._discard_snapshot()
        else:
            s["clients"].append(f)

    def _on_snap_close(self, f, text):
        s = self._snap
        if s is None:
            self._stray_snapshot_line()
        elif (s["settings"] is None or len(s["clients"]) != s["open"]["k"] or
              f["seq"] != s["open"]["seq"]):
            self._discard_snapshot()
        else:
            self._snap = None
            self._apply_snapshot(s)

    def _apply_snapshot(self, s):
        o = s["open"]
        if self.instance is not None and o["instance"] != self.instance:
            # Another daemon, even if its "stato ascolto" was lost.
            self._reset_instance()
            self._event("new_instance", "istanza %s" % o["instance"])
        elif self._last_seq is not None and o["seq"] > self._last_seq + 1:
            # Already corrected by this snapshot: only the list hears of it.
            self._event("missing_snapshots", "istantanee mancanti: %d (seq %d -> %d)"
                        % (o["seq"] - self._last_seq - 1, self._last_seq, o["seq"]))
        self.instance = o["instance"]
        self._last_seq = o["seq"]
        self.daemon_vocabulary = o["vocabulary"]
        self.period_ms = o["period_ms"]
        self.settings = {key: s["settings"][key] for key in SETTINGS}
        self.clients = {}
        for c in s["clients"]:
            self.clients[c["idx"]] = {"idx": c["idx"], "addr": c["addr"], "name": c["name"],
                                      "name_truncated": c["name_truncated"],
                                      "ready": c["state"] == "pronto",
                                      "lat": c["lat"], "peak": c["peak"]}
        self.key_known = True
        if o["holder"] == "libera":
            self.key_holder = None
            self.key_holder_name = None
        else:
            self.key_holder = int(o["holder"])
            holder = self.clients.get(self.key_holder)
            self.key_holder_name = holder["name"] if holder is not None else None
        self.ptt = o["ptt"] == 1
        self.guaranteed = True
