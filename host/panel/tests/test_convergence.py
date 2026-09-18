#!/usr/bin/env python3
"""The closing test of #86: a recorded status stream with lines removed and
its start cut off, and the page still converges to the daemon's real key
holder and PTT, without restarting the daemon.

The stream is fixtures/session.txt, a real cwnetd run (record_stream.py).
Each variant removes an explicit list of lines. Where the daemon would have
confessed the loss, the variant does too, the way status_line() does it: a
`stato stdout N righe scartate` with the cumulative N, in front of the first
line that got through - inside a snapshot if that is where it lands.

A variant is written, at half the recorded pace, into a file that a real
Panel follows (follow.py), and the check reads what comes out of /events
(web.py). At a probe the writer pauses and waits for the /events message
whose version covers every line written so far: the model's version grows
by one per line, so that message shows the page at exactly that point.

What is true comes from outside the stream the panel reads: the PTT level
from --edges at each snapshot (the fixture's checkpoints), the key holder
from the scenario, the client that record_stream.py makes key.

Three rules keep a variant from passing without proving anything (KTD13):
convergence is checked with the key taken and the PTT on; a loss is
confessed as the daemon confesses it; and every variant must fail against
a model that ignores the snapshot (the last test here).

Run from the repository root:
    python3 -m unittest discover -s host/panel/tests -t host/panel -v
"""
import os
import re
import tempfile
import time
import unittest

import cwnetd_panel
import state
from tests.test_web import SSE

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "session.txt")

# Half the recorded pace: every gap between two lines is written at half
# its length. The probes, not the pace, make the windows observable.
PACE = 0.5

# The instance of the spliced second daemon: any value but the fixture's.
SPLICED_INSTANCE = "1789779999-4242"


def load_fixture(path=FIXTURE):
    header, checkpoints, lines = {}, {}, []
    with open(path) as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            m = re.fullmatch(r"# checkpoint seq=(\d+) edges-ptt=([01])", raw)
            if m:
                checkpoints[int(m.group(1))] = int(m.group(2))
            elif raw.startswith("# ") and ": " in raw:
                key, value = raw[2:].split(": ", 1)
                header[key] = value
            elif not raw.startswith("#"):
                ms, _sep, text = raw.partition(" ")
                lines.append((int(ms), text))
    return header, checkpoints, lines


HEADER, CHECKPOINTS, LINES = load_fixture()
TEXTS = [t for _ms, t in LINES]


def index_of(pattern, start=0):
    """First line at or after `start` matching `pattern` in full."""
    rx = re.compile(pattern)
    for i in range(start, len(TEXTS)):
        if rx.fullmatch(TEXTS[i]):
            return i
    raise LookupError(pattern)


def snapshot_span(seq):
    """(index of the opening, index of the closing) of snapshot `seq`."""
    first = index_of(r"stato snap inizio \S+ istanza \S+ seq %d .*" % seq)
    return first, index_of(r"stato snap fine seq %d" % seq, first)


# The scenario's landmarks: client A takes the key and holds it down.
A_IDX = int(re.fullmatch(r"stato connesso client (\d+) IU3QEZ da \S+",
                         TEXTS[index_of(r"stato connesso client \d+ IU3QEZ da \S+")]).group(1))
KEY_A = index_of(r"stato chiave client %d IU3QEZ" % A_IDX)
OVER_A = index_of(r"stato over client %d IU3QEZ B \d+ ms" % A_IDX)
PTT_A = index_of(r"stato ptt 1", KEY_A)
INSTANCE = re.search(r"istanza (\S+)", TEXTS[index_of(r"stato snap inizio .*")]).group(1)


def first_snapshot_after(index, ptt_on=True):
    """The first snapshot opening after `index`, whose closing is a checkpoint
    with the given --edges PTT level."""
    i = index
    while True:
        i = index_of(r"stato snap inizio .*", i + 1)
        seq = int(re.search(r" seq (\d+) ", TEXTS[i]).group(1))
        if CHECKPOINTS[seq] == (1 if ptt_on else 0):
            return seq


def clients_of(seq):
    """{index: name} as snapshot `seq` states them."""
    first, last = snapshot_span(seq)
    out = {}
    for text in TEXTS[first:last]:
        m = re.fullmatch(r"stato snap client \d+/\d+ (\d+) \S+ \S+ lat \S+ peak \S+ nome \d+ ?(.*)",
                         text)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def build(remove=(), confess=True, start=0, cut=0, end=None, extra=()):
    """The stream a reader would see: lines[start:end+1] without `remove`,
    each loss confessed in front of the next line that got through, the
    first line cut `cut` characters in, and `extra` lines appended.

    Items are (ms, text, index in the recording or None)."""
    end = len(LINES) - 1 if end is None else end
    out, dropped, pending = [], 0, False
    for i in range(start, end + 1):
        ms, text = LINES[i]
        if i in remove:
            dropped += 1
            pending = True
            continue
        if pending and confess:
            out.append((ms, "stato stdout %d righe scartate" % dropped, None))
        pending = False
        if i == start and cut:
            text = text[cut:]
        out.append((ms, text, i))
    last_ms = out[-1][0]
    out.extend((last_ms, text, None) for text in extra)
    return out


def position_after(stream, index):
    """1-based position in `stream` of the line with recording index `index`."""
    for pos, (_ms, _text, i) in enumerate(stream, 1):
        if i == index:
            return pos
    raise LookupError(index)


def confession_positions(stream):
    return [pos for pos, (_ms, text, i) in enumerate(stream, 1)
            if i is None and text.startswith("stato stdout ")]


# --- what the page must say ---------------------------------------------

def not_guaranteed(d, where):
    if d["guaranteed"]:
        return ["%s: the page says guaranteed where lines were lost" % where]
    return []


# Failures that any model without the snapshot has in every variant: they
# say nothing about the loss a variant was built around.
SNAPSHOT_ONLY = re.compile(r": (instance|settings unknown)")


def truth_at(seq, holder=A_IDX, instance=INSTANCE):
    """The page at snapshot `seq`: guaranteed, the scenario's key holder,
    the PTT of --edges, the clients and every setting."""
    ptt = CHECKPOINTS[seq] == 1
    clients = clients_of(seq)

    def check(d, where):
        failures = []
        if not d["guaranteed"]:
            failures.append("%s: not guaranteed" % where)
        if d["instance"] != instance:
            failures.append("%s: instance %r, want %r" % (where, d["instance"], instance))
        if not d["key_known"] or d["key_holder"] != holder:
            failures.append("%s: key holder %r, want %r" % (where, d["key_holder"], holder))
        if d["ptt"] is not ptt:
            failures.append("%s: PTT %r, --edges says %r" % (where, d["ptt"], ptt))
        got = {c["idx"]: c["name"] for c in d["clients"]}
        if got != clients:
            failures.append("%s: clients %r, want %r" % (where, got, clients))
        missing = [k for k, v in d["settings"].items() if v is None]
        if missing:
            failures.append("%s: settings unknown: %s" % (where, missing))
        return failures
    return check


# --- the variants ---------------------------------------------------------

def variant_ae1():
    """AE1: the key line and the PTT line lost mid-over, each confessed."""
    seq = first_snapshot_after(PTT_A)
    stream = build(remove={KEY_A, PTT_A}, end=snapshot_span(seq)[1])
    probes = {confession_positions(stream)[-1]: [not_guaranteed]}
    return stream, probes, truth_at(seq)


def variant_ae2():
    """AE2: the stream starts mid-line after the key was taken; `stato
    ascolto`, the key line and a whole snapshot's opening are not in it."""
    s6_first, _s6_last = snapshot_span(first_snapshot_after(KEY_A, ptt_on=False))
    seq = first_snapshot_after(PTT_A)
    stream = build(start=s6_first + 1, cut=20, end=snapshot_span(seq)[1])
    probes = {position_after(stream, PTT_A): [not_guaranteed]}
    return stream, probes, truth_at(seq)


def variant_spliced_restart():
    """A second daemon spliced in without its `stato ascolto`: its first
    snapshot, with no client, must remove every client of the first."""
    seq = first_snapshot_after(PTT_A)
    first, last = snapshot_span(1)
    spliced = [t.replace("istanza %s" % INSTANCE, "istanza %s" % SPLICED_INSTANCE)
               for t in TEXTS[first:last + 1]]
    stream = build(end=snapshot_span(seq)[1], extra=spliced)
    return stream, {}, truth_at(1, holder=None, instance=SPLICED_INSTANCE)


def variant_cut_snapshot():
    """The second half of a snapshot lost, and the PTT line after it: the
    confession lands inside the snapshot, which is discarded."""
    s6_first, s6_last = snapshot_span(first_snapshot_after(KEY_A, ptt_on=False))
    seq = first_snapshot_after(PTT_A)
    remove = set(range(s6_first + 2, s6_last + 1)) | {PTT_A}
    stream = build(remove=remove, end=snapshot_span(seq)[1])
    probes = {confession_positions(stream)[-1]: [not_guaranteed]}
    return stream, probes, truth_at(seq)


def variant_confession_only():
    """A line lost that changes nothing on the page: the confession alone
    must make the state not guaranteed until the next snapshot."""
    seq = first_snapshot_after(PTT_A)
    stream = build(remove={OVER_A}, end=snapshot_span(seq)[1])
    probes = {confession_positions(stream)[-1]: [not_guaranteed]}
    return stream, probes, truth_at(seq)


def variant_unconfessed():
    """The key and PTT lines lost without a confession (journald's rate
    limit): convergence within one period, nothing asserted on the
    declaration, because nothing in the stream says a line was lost."""
    seq = first_snapshot_after(PTT_A)
    stream = build(remove={KEY_A, PTT_A}, confess=False, end=snapshot_span(seq)[1])
    return stream, {}, truth_at(seq)


VARIANTS = {
    "ae1_key_and_ptt_lines_lost": variant_ae1,
    "ae2_start_cut_mid_line": variant_ae2,
    "spliced_restart_without_ascolto": variant_spliced_restart,
    "cut_in_the_middle_of_a_snapshot": variant_cut_snapshot,
    "confession_with_no_other_loss": variant_confession_only,
    "lines_removed_without_a_confession": variant_unconfessed,
}


# --- running a variant ------------------------------------------------------

def run_through_the_panel(stream, probes, final):
    """File followed by a real Panel, state read from /events."""
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "cwnetd.log")
        panel = cwnetd_panel.Panel(port=0, follow_path=path).start()
        sse = None
        try:
            sse = SSE(panel.port)
            with open(path, "ab", buffering=0) as fh:
                prev = stream[0][0]
                for pos, (ms, text, _i) in enumerate(stream, 1):
                    time.sleep(max(0, ms - prev) * PACE / 1000.0)
                    prev = ms
                    fh.write((text + "\n").encode("ascii"))
                    for check in probes.get(pos, ()):
                        d = sse.state_until(lambda d, p=pos: d["version"] >= p)
                        failures += check(d, "after line %d" % pos)
            d = sse.state_until(lambda d: d["version"] >= len(stream))
            failures += final(d, "at the end")
        finally:
            if sse is not None:
                sse.close()
            panel.stop()
    return failures


def run_on_a_model(model, stream, probes, final):
    """The same variant fed straight to a model, for the check below."""
    failures = []
    for pos, (ms, text, _i) in enumerate(stream, 1):
        model.apply(text, False, ms / 1000.0)
        for check in probes.get(pos, ()):
            failures += check(model.to_dict(), "after line %d" % pos)
    return failures + final(model.to_dict(), "at the end")


class SnapshotIgnoringModel(state.Model):
    """A panel without the snapshot: it rebuilds the state from the event
    lines alone and takes it as true, which is what #86 says cannot work."""

    def _reset_instance(self):
        super()._reset_instance()
        self.guaranteed = True

    def _not_guaranteed(self):
        pass

    def _apply_snapshot(self, s):
        pass


class FixtureTest(unittest.TestCase):

    def test_fixture_speaks_the_panels_vocabulary(self):
        self.assertEqual(HEADER["vocabulary"], state.VOCABULARY,
                         "regenerate fixtures/session.txt with record_stream.py")

    def test_fixture_snapshots_agree_with_edges_and_with_the_scenario(self):
        self.assertEqual(int(HEADER["snapshot-ms"]), 250)
        for seq, edges_ptt in CHECKPOINTS.items():
            first, _last = snapshot_span(seq)
            declared = int(re.search(r" ptt ([01]) ", TEXTS[first]).group(1))
            self.assertEqual(declared, edges_ptt, "snapshot %d" % seq)
        over = [seq for seq, level in CHECKPOINTS.items()
                if snapshot_span(seq)[0] > PTT_A and level == 1
                and snapshot_span(seq)[0] < index_of(r"stato chiave libera", PTT_A)]
        self.assertGreaterEqual(len(over), 4, "A's over must span several snapshots")
        for seq in over:
            self.assertIn(" chiave %d " % A_IDX, TEXTS[snapshot_span(seq)[0]])
        index_of(r"stato rifiutato da \S+: nessuno slot libero")
        index_of(r"stato fault client %d IU3QEZ: titolare sparito a meta' over" % A_IDX)
        index_of(r"stato connesso client \d+ B2 <b>x</b> da \S+")


class ConvergenceTest(unittest.TestCase):
    """Each variant through the whole chain: file, follower, model, /events."""

    def run_variant(self, name):
        failures = run_through_the_panel(*VARIANTS[name]())
        self.assertEqual(failures, [])

    def test_ae1_key_and_ptt_lines_lost_mid_over_converge_to_the_real_holder_and_edges_ptt(self):
        self.run_variant("ae1_key_and_ptt_lines_lost")

    def test_ae2_start_cut_mid_line_without_ascolto_converges_at_the_first_snapshot(self):
        self.run_variant("ae2_start_cut_mid_line")

    def test_spliced_restart_without_ascolto_resets_to_the_new_instance(self):
        self.run_variant("spliced_restart_without_ascolto")

    def test_snapshot_cut_in_the_middle_is_discarded_and_the_next_one_applied(self):
        self.run_variant("cut_in_the_middle_of_a_snapshot")

    def test_confession_alone_declares_the_state_not_guaranteed_until_the_snapshot(self):
        self.run_variant("confession_with_no_other_loss")

    def test_lines_removed_without_a_confession_converge_within_one_period(self):
        self.run_variant("lines_removed_without_a_confession")

    def test_every_variant_fails_against_a_model_that_ignores_the_snapshot(self):
        for name, make in VARIANTS.items():
            with self.subTest(variant=name):
                failures = run_on_a_model(SnapshotIgnoringModel(), *make())
                about_the_loss = [f for f in failures if not SNAPSHOT_ONLY.search(f)]
                self.assertNotEqual(about_the_loss, [],
                                    "this variant proves nothing: its loss does not show "
                                    "without the snapshot")
                self.assertEqual(run_on_a_model(state.Model(), *make()), [])


if __name__ == "__main__":
    unittest.main()
