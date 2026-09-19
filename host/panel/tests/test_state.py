#!/usr/bin/env python3
"""Tests for state.py: cwnetd status lines in, the state the page shows out.

Every line here is written in the shape of host/cwnetd/README.md, "Reading a
status line", which is the contract. The lines in RealDaemonLinesTest were
copied from a run of the built daemon, so a drift between the README and
main.c shows up here and not only in the live test.

Time is injected: `now` is a monotonic instant in seconds, and the wall
clock that stamps the events is a fake.

Run from the repository root:
    python3 -m unittest discover -s host/panel/tests -t host/panel -v
"""
import json
import unittest

import state

MANOPOLE = ("stato snap manopole max-clients 4 B>= 100 tetto 1000 coda 100 lead 0 "
            "idle 5000 over-max 120000 handshake 5000 out-cap 16384")
ASCOLTO = ("stato ascolto 127.0.0.1:7355 max-clients 4 B>=100 ms tetto 1000 ms coda 100 ms "
           "lead 0 ms out-cap 16384 byte")
INSTANCE = "1789769762-38671"


def snap(seq, clients=(), holder="libera", ptt=0, period=5000, instance=INSTANCE,
         version="v1", manopole=MANOPOLE):
    """One snapshot, line by line, as snapshot_write() in main.c writes it.

    clients: (idx, "pronto"|"attesa", addr, lat, peak, name) per open socket."""
    k = len(clients)
    lines = ["stato snap inizio %s istanza %s seq %d client %d chiave %s ptt %d periodo %d"
             % (version, instance, seq, k, holder, ptt, period),
             manopole]
    for i, (idx, st, addr, lat, peak, name) in enumerate(clients, 1):
        lines.append("stato snap client %d/%d %d %s %s lat %d peak %d nome %d %s"
                     % (i, k, idx, st, addr, lat, peak, len(name), name))
    lines.append("stato snap fine seq %d" % seq)
    return lines


C1 = (1, "pronto", "10.0.0.1:50001", 12, 15, "IU3QEZ")
C2 = (2, "pronto", "10.0.0.2:50002", 20, 22, "DL4YHF")


def check_ids(d):
    """Every id the page looks up is in the set state.py exports for it: the
    coverage check in test_web reads those sets, not the model."""
    sent = [("banner", b, state.BANNER_IDS) for b in d["banners"]]
    sent += [("liveness", d["liveness"], state.LIVENESS_VALUES),
             ("key state", d["key_view"]["state"], state.KEY_STATES),
             ("PTT state", d["ptt_view"], state.PTT_STATES)]
    sent += [("fitness", c["fitness"], state.FITNESS_VALUES) for c in d["clients"]]
    if d["output"]["edges"] is not None:
        sent.append(("edges destination", d["output"]["edges"], state.EDGES_DESTINATIONS))
    for what, value, ids in sent:
        assert value in ids, "%s %r is not in %s" % (what, value, sorted(ids))


class Feed:
    """A model and a clock: every live line arrives `dt` seconds after the last."""

    def __init__(self):
        self.now = 1000.0
        self.m = state.Model(wall=lambda: 1789769762.0)

    def live(self, *lines, dt=0.01):
        for line in lines:
            self.now += dt
            self.m.apply(line, False, self.now)
        return self

    def backlog(self, *lines):
        for line in lines:
            self.m.apply(line, True, self.now)
        return self

    def tick(self, seconds):
        self.now += seconds
        self.m.tick(self.now)
        return self

    @property
    def d(self):
        d = self.m.to_dict()
        check_ids(d)
        return d

    def kinds(self):
        return [e["kind"] for e in self.d["events"]]


def guaranteed_with(*clients, holder="libera", ptt=0, seq=1):
    f = Feed().live(ASCOLTO, *snap(seq, clients, holder=holder, ptt=ptt))
    assert f.d["guaranteed"], "setup: a complete snapshot must make the state guaranteed"
    return f


class SnapshotTest(unittest.TestCase):

    def test_complete_snapshot_makes_the_state_guaranteed_and_carries_it_all(self):
        f = Feed().live(*snap(1, [C1, C2], holder="1", ptt=1))
        d = f.d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["key_holder"], 1)
        self.assertEqual(d["key_holder_name"], "IU3QEZ")
        self.assertTrue(d["ptt"])
        self.assertEqual([c["idx"] for c in d["clients"]], [1, 2])
        self.assertEqual(d["clients"][1]["name"], "DL4YHF")
        self.assertEqual(d["clients"][1]["addr"], "10.0.0.2:50002")
        self.assertEqual(d["clients"][0]["lat_ms"], 12)
        self.assertEqual(d["clients"][0]["peak_ms"], 15)
        self.assertTrue(d["clients"][0]["ready"])
        self.assertEqual(d["settings"]["idle_ms"], 5000)
        self.assertEqual(d["settings"]["out_cap_bytes"], 16384)
        self.assertEqual(d["instance"], INSTANCE)
        self.assertEqual(d["period_ms"], 5000)

    def test_ae3_snapshot_missing_one_client_line_is_discarded_and_state_not_guaranteed(self):
        lines = snap(1, [C1, C2], holder="2", ptt=1)
        del lines[3]  # the 2/2 line
        f = Feed().live(*lines)
        self.assertFalse(f.d["guaranteed"])
        self.assertIsNone(f.d["key_holder"])
        self.assertFalse(f.d["key_known"])
        self.assertEqual(f.d["clients"], [])

    def test_complete_snapshot_wins_over_contradicting_events_and_removes_absent_clients(self):
        f = guaranteed_with(C1, C2, holder="1", ptt=1)
        f.live("stato chiave client 2 DL4YHF", "stato ptt 0",
               "stato accettato client 3 da 10.0.0.3:50003")
        f.live(*snap(2, [C1], holder="1", ptt=1))
        d = f.d
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["ptt"])
        self.assertEqual([c["idx"] for c in d["clients"]], [1])

    def test_event_line_between_opening_and_closing_cancels_the_snapshot(self):
        lines = snap(1, [C1], holder="1", ptt=1)
        lines.insert(2, "stato latenza client 1 12 ms peak 15 ms")
        f = Feed().live(*lines)
        self.assertFalse(f.d["guaranteed"])
        self.assertIsNone(f.d["key_holder"])
        self.assertIn("discarded_snapshot", f.kinds())

    def test_client_positions_out_of_order_discard_the_snapshot(self):
        lines = snap(1, [C1, C2, (3, "attesa", "10.0.0.3:50003", -1, -1, "")])
        lines[2], lines[3] = lines[3], lines[2]  # 2/3 before 1/3
        f = Feed().live(*lines)
        self.assertFalse(f.d["guaranteed"])
        self.assertEqual(f.d["clients"], [])

    def test_new_opening_before_the_closing_discards_the_first_and_applies_the_second(self):
        first = snap(1, [C1, C2])[:3]
        f = Feed().live(*first, *snap(2, [C2], holder="2"))
        self.assertTrue(f.d["guaranteed"])
        self.assertEqual(f.d["key_holder"], 2)
        self.assertEqual([c["idx"] for c in f.d["clients"]], [2])

    def test_closing_with_another_sequence_discards_the_snapshot(self):
        lines = snap(4, [C1])
        lines[-1] = "stato snap fine seq 5"
        f = Feed().live(*lines)
        self.assertFalse(f.d["guaranteed"])

    def test_sequence_gap_between_complete_snapshots_stays_guaranteed_and_records_missing(self):
        f = Feed().live(*snap(5, [C1]), *snap(7, [C1]))
        self.assertTrue(f.d["guaranteed"])
        self.assertIn("missing_snapshots", f.kinds())

    def test_consecutive_sequence_records_nothing_missing(self):
        f = Feed().live(*snap(5, [C1]), *snap(6, [C1]))
        self.assertNotIn("missing_snapshots", f.kinds())

    def test_client_line_cut_at_the_line_cap_marks_the_name_truncated_and_applies_the_rest(self):
        name = "X" + "\\x1B" * 60  # 241 escaped characters: more than the line holds
        head = "stato snap client 1/1 1 pronto 10.0.0.1:50001 lat 12 peak 15 nome %d " % len(name)
        line = (head + name)[:state.LINE_MAX]
        self.assertEqual(len(line), 254)
        lines = snap(1, [], holder="1")
        lines[0] = lines[0].replace("client 0", "client 1")
        lines.insert(2, line)
        f = Feed().live(*lines)
        d = f.d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(len(d["clients"]), 1)
        self.assertTrue(d["clients"][0]["name_truncated"])
        self.assertTrue(name.startswith(d["clients"][0]["name"]))
        self.assertEqual(d["clients"][0]["lat_ms"], 12)
        self.assertEqual(d["key_holder"], 1)

    def test_client_line_whose_name_is_longer_than_declared_is_unreadable_and_discards(self):
        lines = snap(1, [C1], holder="1")
        lines[2] = lines[2].replace("nome 6 IU3QEZ", "nome 3 IU3QEZ")
        f = Feed().live(*lines)
        self.assertFalse(f.d["guaranteed"])
        self.assertEqual(f.d["clients"], [])
        self.assertIn("unreadable", f.kinds())

    def test_name_shorter_than_declared_below_the_cap_is_a_glued_half_line_not_a_cut(self):
        # A short write cut the line inside the name, and the confession that
        # status_line() writes next was glued to what got out.
        lines = snap(1, [C1], holder="1", ptt=1)
        glued = "stato snap client 1/1 1 pronto 10.0.0.1:50001 lat 12 peak 15 nome 6 IU3" \
                "stato stdout 1 righe scartate"
        f = Feed().live(lines[0], lines[1], glued, lines[3])
        self.assertFalse(f.d["guaranteed"])
        self.assertIn("dropped_lines", f.kinds())

    def test_long_name_cut_early_with_a_confession_glued_is_not_read_as_a_cut_name(self):
        # Here what follows "nome 40 " is shorter than 40: only a line at the
        # 254-character cap was cut by the daemon, so this one was not.
        long_name = "IU3QEZ/P " + "x" * 31
        self.assertEqual(len(long_name), 40)
        lines = snap(1, [(1, "pronto", "10.0.0.1:50001", 12, 15, long_name)], holder="1")
        glued = "stato snap client 1/1 1 pronto 10.0.0.1:50001 lat 12 peak 15 nome 40 IU3" \
                "stato stdout 1 righe scartate"
        self.assertLess(len("IU3stato stdout 1 righe scartate"), 40)
        f = Feed().live(lines[0], lines[1], glued, lines[3])
        self.assertFalse(f.d["guaranteed"])
        self.assertEqual(f.d["clients"], [])
        self.assertIn("dropped_lines", f.kinds())

    def test_name_whose_trailing_spaces_journald_trimmed_is_restored_and_applied(self):
        # journald strips a line's trailing whitespace, and sanitize() lets a
        # space through as it is: "nome 8 IU3QEZ  " arrives as "nome 8 IU3QEZ".
        for name in ("IU3QEZ  ", "  "):
            with self.subTest(name=name):
                lines = snap(1, [(1, "pronto", "10.0.0.1:50001", 12, 15, name)], holder="1")
                lines[2] = lines[2].rstrip(" ")
                self.assertLess(len(lines[2]), state.LINE_MAX)
                d = Feed().live(*lines).d
                self.assertTrue(d["guaranteed"])
                self.assertEqual(d["clients"][0]["name"], name)
                self.assertFalse(d["clients"][0]["name_truncated"])
                self.assertEqual(d["key_holder_name"], name)

    def test_line_cut_at_the_cap_on_a_space_journald_trimmed_is_a_cut_name(self):
        # The daemon cut the line at 254 characters, the last of them a space,
        # and journald stripped it: only that space is restored, and the name
        # is marked cut, not padded to the declared length.
        head = "stato snap client 1/1 1 pronto 10.0.0.1:50001 lat 12 peak 15 nome 191 "
        room = state.LINE_MAX - len(head)
        name = "A" * (room - 1) + " " + "B" * (191 - room)
        cut = (head + name)[:state.LINE_MAX]
        self.assertEqual(cut[-2:], "A ")
        lines = snap(1, [], holder="1")
        lines[0] = lines[0].replace("client 0", "client 1")
        lines.insert(2, cut.rstrip(" "))
        d = Feed().live(*lines).d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["clients"][0]["name"], cut[len(head):])
        self.assertTrue(d["clients"][0]["name_truncated"])

    def test_name_containing_stato_stdout_is_read_whole_in_a_snapshot(self):
        tricky = (1, "pronto", "10.0.0.1:50001", 12, 15, "x stato stdout 9 righe scartate")
        f = Feed().live(*snap(1, [tricky], holder="1"))
        self.assertTrue(f.d["guaranteed"])
        self.assertEqual(f.d["clients"][0]["name"], "x stato stdout 9 righe scartate")
        self.assertFalse(f.d["clients"][0]["name_truncated"])

    def test_attesa_client_with_empty_name(self):
        f = Feed().live(*snap(1, [(1, "attesa", "10.0.0.1:50001", -1, -1, "")]))
        c = f.d["clients"][0]
        self.assertFalse(c["ready"])
        self.assertEqual(c["name"], "")
        self.assertIsNone(c["lat_ms"])
        self.assertIsNone(c["peak_ms"])

    def test_snapshot_of_another_instance_resets_the_state_and_is_applied(self):
        f = guaranteed_with(C1, C2, holder="1", ptt=1)
        f.live("stato over client 1 IU3QEZ B 100 ms")
        f.live(*snap(1, [C2], holder="libera", instance="1789770000-99"))
        d = f.d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["instance"], "1789770000-99")
        self.assertEqual([c["idx"] for c in d["clients"]], [2])
        self.assertIsNone(d["key_holder"])
        self.assertTrue(d["key_known"])
        self.assertNotIn("missing_snapshots", f.kinds())
        self.assertEqual(f.kinds().count("new_instance"), 2)

    def test_vocabulary_v2_in_the_opening_raises_the_banner_and_still_applies(self):
        f = Feed().live(*snap(1, [C1], holder="1", version="v2"))
        d = f.d
        self.assertIn("vocabulary", d["banners"])
        self.assertEqual(d["daemon_vocabulary"], "v2")
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["key_holder"], 1)
        f.live("stato chiave libera")
        self.assertIsNone(f.d["key_holder"])

    def test_opening_of_a_vocabulary_this_panel_cannot_read_raises_the_banner(self):
        # The shape of the opening changed with the version: the panel cannot
        # read the snapshot, and says why instead of only "unreadable".
        f = guaranteed_with(C1)
        f.live(snap(2, [C1])[0], "stato snap inizio v2 istanza 1-2 seq 1 nuovo-campo 3")
        d = f.d
        self.assertIn("vocabulary", d["banners"])
        self.assertEqual(d["daemon_vocabulary"], "v2")
        self.assertFalse(d["guaranteed"])
        self.assertIn("discarded_snapshot", f.kinds())

    def test_opening_of_this_vocabulary_in_another_shape_stays_unreadable(self):
        f = guaranteed_with(C1)
        f.live("stato snap inizio v1 istanza 1-2 seq 1 nuovo-campo 3")
        d = f.d
        self.assertNotIn("vocabulary", d["banners"])
        self.assertFalse(d["guaranteed"])
        self.assertIn("unreadable", f.kinds())

    def test_period_comes_from_the_last_applied_snapshot(self):
        f = Feed().live(*snap(1, [], period=250))
        self.assertEqual(f.d["period_ms"], 250)


class GuaranteeTest(unittest.TestCase):

    def test_state_is_not_guaranteed_before_the_first_snapshot(self):
        f = Feed().live(ASCOLTO, "stato chiave client 1 IU3QEZ", "stato ptt 1")
        self.assertFalse(f.d["guaranteed"])
        self.assertEqual(f.d["key_holder"], 1)
        self.assertTrue(f.d["ptt"])
        self.assertIn("not_guaranteed", f.d["banners"])

    def test_confession_already_seen_changes_nothing_and_a_larger_one_breaks_the_guarantee(self):
        f = Feed().live(ASCOLTO, "stato stdout 3 righe scartate", *snap(1, [C1]))
        self.assertTrue(f.d["guaranteed"])
        f.live("stato stdout 3 righe scartate")
        self.assertTrue(f.d["guaranteed"])
        f.live("stato stdout 4 righe scartate")
        self.assertFalse(f.d["guaranteed"])
        self.assertEqual(f.kinds().count("dropped_lines"), 2)

    def test_lost_events_break_the_guarantee_until_the_next_snapshot(self):
        f = guaranteed_with(C1)
        f.live("stato eventi persi 1")
        self.assertFalse(f.d["guaranteed"])
        self.assertIn("lost_events", f.kinds())
        f.live(*snap(2, [C1]))
        self.assertTrue(f.d["guaranteed"])

    def test_level_lines_keep_the_state_guaranteed_while_ptt_alternates(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        for level in (0, 1, 0, 1, 0):
            f.live("stato ptt %d" % level)
            self.assertTrue(f.d["guaranteed"])
            self.assertEqual(f.d["key_holder"], 1)

    def test_half_key_line_glued_to_a_confession_is_unreadable_and_the_confession_applies(self):
        f = guaranteed_with(C1, holder="libera")
        f.live("stato chiave client 1 IU3stato stdout 1 righe scartate")
        d = f.d
        self.assertFalse(d["guaranteed"])
        self.assertIsNone(d["key_holder"], "the half line must not be read as a key line")
        self.assertIn("unreadable", f.kinds())
        self.assertIn("dropped_lines", f.kinds())

    def test_glued_tail_that_is_not_a_complete_confession_is_unreadable(self):
        f = guaranteed_with(C1)
        f.live("stato chiave client 1 IU3stato stdout 1 righe")
        self.assertFalse(f.d["guaranteed"])
        self.assertNotIn("dropped_lines", f.kinds())

    def test_callsign_carrying_a_huge_confession_cannot_silence_later_real_ones(self):
        f = guaranteed_with(C1)
        f.live("stato chiave client 1 x stato stdout 999999 righe scartate")
        f.live(*snap(2, [C1], holder="1"))
        self.assertTrue(f.d["guaranteed"])
        f.live("stato stdout 2 righe scartate")
        self.assertFalse(f.d["guaranteed"], "a real confession after the callsign must count")

    def test_shutdown_total_is_not_a_new_confession(self):
        f = Feed().live("stato stdout 7 righe scartate", *snap(1, [C1]))
        f.live("stato stdout 7 righe scartate in totale")
        self.assertTrue(f.d["guaranteed"])
        self.assertIn("dropped_total", f.kinds())

    def test_unreadable_line_of_a_known_kind_breaks_the_guarantee(self):
        f = guaranteed_with(C1)
        f.live("stato latenza client 1 12 ms")
        self.assertFalse(f.d["guaranteed"])
        self.assertIn("unreadable", f.kinds())

    def test_ascolto_mid_stream_resets_the_state_reads_the_settings_and_is_not_guaranteed(self):
        f = guaranteed_with(C1, C2, holder="1", ptt=1)
        f.live("stato ascolto 0.0.0.0:7355 max-clients 2 B>=50 ms tetto 800 ms coda 150 ms "
               "lead 10 ms out-cap 4096 byte")
        d = f.d
        self.assertFalse(d["guaranteed"])
        self.assertEqual(d["clients"], [])
        self.assertFalse(d["key_known"])
        self.assertIsNone(d["ptt"])
        self.assertIsNone(d["instance"])
        self.assertEqual(d["listen"], "0.0.0.0:7355")
        s = d["settings"]
        self.assertEqual((s["max_clients"], s["play_floor_ms"], s["link_ceiling_ms"],
                          s["ptt_tail_ms"], s["ptt_lead_ms"], s["out_cap_bytes"]),
                         (2, 50, 800, 150, 10, 4096))
        self.assertIsNone(s["idle_ms"])
        self.assertEqual(f.kinds().count("new_instance"), 2)


class LineTest(unittest.TestCase):

    def test_names_with_da_colon_hash_markup_and_escapes_are_read_whole_as_text(self):
        name = "A da B: #1 <b>X</b>\\x1B"
        f = Feed().live("stato accettato client 1 da 10.0.0.1:50001",
                        "stato connesso client 1 %s da 10.0.0.1:50001" % name)
        c = f.d["clients"][0]
        self.assertEqual(c["name"], name)
        self.assertTrue(c["ready"])
        self.assertEqual(c["addr"], "10.0.0.1:50001")
        f.live("stato disconnesso client 1 %s da 10.0.0.1:50001: tre PING senza risposta" % name)
        self.assertEqual(f.d["clients"], [])
        ev = f.d["events"][-1]
        self.assertEqual(ev["kind"], "disconnected")
        self.assertIn(name, ev["text"])
        self.assertTrue(ev["text"].endswith(": tre PING senza risposta"))

    def test_ae5_markup_and_escape_in_a_callsign_reach_the_json_as_the_same_string(self):
        f = Feed().live(*snap(1, [(1, "pronto", "10.0.0.1:50001", 1, 1, "<b>X</b>\\x1B")],
                              holder="1"))
        text = json.dumps(f.d)
        self.assertEqual(json.loads(text)["clients"][0]["name"], "<b>X</b>\\x1B")
        self.assertEqual(json.loads(text)["key_holder_name"], "<b>X</b>\\x1B")

    def test_callsign_x_stato_arresto_taking_the_key_does_not_stop_the_page(self):
        f = guaranteed_with(C1)
        f.live("stato chiave client 1 x stato arresto")
        d = f.d
        self.assertEqual(d["liveness"], "alive")
        self.assertEqual(d["key_holder"], 1)
        self.assertEqual(d["key_holder_name"], "x stato arresto")

    def test_accettato_on_the_same_index_with_another_address_opens_a_new_client(self):
        f = guaranteed_with(C1)
        f.live("stato latenza client 1 300 ms peak 1500 ms",
               "stato accettato client 1 da 10.0.0.9:50009")
        c = f.d["clients"][0]
        self.assertEqual(c["addr"], "10.0.0.9:50009")
        self.assertEqual(c["name"], "")
        self.assertFalse(c["ready"])
        self.assertIsNone(c["peak_ms"])

    def test_disconnection_of_another_address_on_the_index_leaves_the_client(self):
        f = guaranteed_with(C1)
        f.live("stato disconnesso client 1 OLD da 10.0.0.8:50008: chiuso dal peer")
        self.assertEqual([c["idx"] for c in f.d["clients"]], [1])
        self.assertIn("disconnected", f.kinds())

    def test_fault_goes_to_the_events_and_leaves_the_key_holder(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        f.live("stato fault client 1 IU3QEZ: titolare sparito a meta' over")
        self.assertEqual(f.d["key_holder"], 1)
        self.assertTrue(f.d["guaranteed"])
        self.assertEqual(f.d["events"][-1]["kind"], "fault")
        f.live("stato fault: over oltre il tetto")
        self.assertEqual(f.d["events"][-1]["kind"], "fault")

    def test_peak_over_the_ceiling_marks_the_link_unfit_and_below_it_does_not(self):
        f = guaranteed_with(C1, C2)
        f.live("stato latenza client 1 300 ms peak 1200 ms",
               "stato latenza client 2 300 ms peak 900 ms")
        c1, c2 = f.d["clients"]
        self.assertEqual(c1["fitness"], "unfit")
        self.assertEqual(c2["fitness"], "fit")

    def test_edges_path_never_reaches_state_events_or_json(self):
        path = "/Users/x/fronti.log"
        f = Feed().live(ASCOLTO, "stato uscita virtual fronti %s" % path, *snap(1, [C1]))
        f.live("stato uscita fronti (%s) non drena: 150 ms e aspetto" % path,
               "stato uscita fronti (%s) rifiuta: Broken pipe" % path,
               "stato arresto",
               "stato uscita fronti (%s): 2 attese per 300 ms, 1 errori, 0 persi" % path)
        d = f.d
        self.assertEqual(d["output"], {"backend": "virtual", "edges": "file"})
        self.assertEqual(f.kinds()[-3:], ["edges_refused", "stopped", "edges_summary"])
        self.assertIn("edges_stall", f.kinds())
        text = json.dumps(d)
        self.assertNotIn("/Users", text)
        self.assertNotIn("fronti.log", text)

    def test_edges_to_stderr_are_named_as_stderr(self):
        f = Feed().live("stato uscita virtual fronti stderr")
        self.assertEqual(f.d["output"], {"backend": "virtual", "edges": "stderr"})

    def test_edge_line_in_the_stream_is_ignored_and_raises_the_mixed_edges_banner(self):
        f = guaranteed_with(C1)
        f.live("ptt 1 123", "key 1 123")
        d = f.d
        self.assertIs(d["ptt"], False, "an edge line is not a PTT level")
        self.assertTrue(d["guaranteed"])
        self.assertIn("mixed_edges", d["banners"])

    def test_edge_line_read_from_a_regular_file_raises_no_banner(self):
        # 2>&1 into a regular file cannot stall edge_write(): the panel is told
        # it reads one, and the edge line changes nothing.
        m = state.Model(wall=lambda: 1789769762.0, warn_mixed_edges=False)
        m.apply("ptt 1 123", False, 1000.0)
        self.assertNotIn("mixed_edges", m.to_dict()["banners"])

    def test_event_of_a_backlog_line_has_no_time_and_of_a_live_line_the_wall_clock(self):
        # A backlog line can be hours old, and carries no time of its own.
        line = "stato fault: over oltre il tetto"
        f = Feed().backlog(line)
        self.assertIsNone(f.d["events"][-1]["t"])
        f.live(line)
        self.assertEqual(f.d["events"][-1]["t"], 1789769762.0)

    def test_startup_message_and_other_non_status_lines_are_ignored(self):
        f = guaranteed_with(C1)
        before = f.d
        f.live("cwnetd: --edges /x: Permission denied", "", "hello")
        after = f.d
        for key in ("guaranteed", "clients", "key_holder", "ptt", "events", "banners"):
            self.assertEqual(before[key], after[key], key)

    def test_unknown_stato_line_goes_to_the_events_and_changes_nothing_else(self):
        f = guaranteed_with(C1, holder="1")
        f.live("stato qualcosa di nuovo")
        self.assertTrue(f.d["guaranteed"])
        self.assertEqual(f.d["key_holder"], 1)
        self.assertEqual(f.d["events"][-1]["kind"], "unknown")
        self.assertEqual(f.d["events"][-1]["text"], "qualcosa di nuovo")

    def test_event_list_keeps_the_last_fifty(self):
        f = Feed()
        for i in range(state.EVENTS_MAX + 7):
            f.live("stato rifiutato da 10.0.0.%d:5000: nessuno slot libero" % (i % 250))
        d = f.d
        self.assertEqual(len(d["events"]), state.EVENTS_MAX)
        self.assertEqual(d["events_max"], 50)
        self.assertIn("10.0.0.56:5000", d["events"][-1]["text"])

    def test_every_line_kind_in_the_readme_tables_is_recognised(self):
        # One example per row of host/cwnetd/README.md, "Reading a status line".
        examples = [
            ASCOLTO,
            "stato uscita virtual fronti stderr",
            "stato accettato client 1 da 10.0.0.1:50001",
            "stato rifiutato da 10.0.0.5:50005: nessuno slot libero",
            "stato connesso client 1 IU3QEZ da 10.0.0.1:50001",
            "stato disconnesso client 1 IU3QEZ da 10.0.0.1:50001: chiuso dal peer",
            "stato client 1 lettore fermo: 16380 byte non inviati, chiudo",
            "stato slot client 1 ancora in uso da 10.0.0.1:50001: chiudo la connessione precedente",
            "stato accept fallita: Too many open files",
            "stato chiave client 1 IU3QEZ",
            "stato chiave libera",
            "stato ptt 1",
            "stato ptt 0",
            "stato latenza client 1 12 ms peak 15 ms",
            "stato link non idoneo client 1 IU3QEZ: peak 1200 ms",
            "stato over client 1 IU3QEZ B 100 ms",
            "stato byte in ritardo client 1 IU3QEZ: 3 byte, 40 ms in totale",
            "stato fault client 1 IU3QEZ: over oltre il tetto",
            "stato fault: titolare muto oltre l'inattivita' col tasto su",
            "stato eventi persi 2",
            "stato stdout 3 righe scartate",
            "stato arresto",
            "stato stdout 3 righe scartate in totale",
            "stato uscita fronti (stderr): 1 attese per 120 ms, 0 errori, 0 persi",
            "stato uscita fronti (stderr) non drena: 100 ms e aspetto",
            "stato uscita fronti (stderr) rifiuta: Broken pipe",
            "stato poll fallita: Bad file descriptor",
        ] + snap(1, [C1])
        for line in examples:
            kinds = [p.kind for p in state.parse(line)]
            self.assertEqual(len(kinds), 1, line)
            self.assertNotIn(kinds[0], ("unknown", "unreadable", "ignored"), line)


class LivenessTest(unittest.TestCase):

    def test_waiting_until_the_first_live_line_then_alive(self):
        f = Feed()
        self.assertEqual(f.d["liveness"], "waiting")
        self.assertIn("waiting", f.d["banners"])
        f.live("stato ptt 0")
        self.assertEqual(f.d["liveness"], "alive")

    def test_backlog_lines_apply_the_state_and_leave_liveness_waiting(self):
        f = Feed().backlog(ASCOLTO, *snap(1, [C1], holder="1", ptt=1))
        d = f.d
        self.assertEqual(d["liveness"], "waiting")
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["guaranteed"])
        self.assertTrue(d["stale"])
        f.tick(60)
        self.assertEqual(f.d["liveness"], "waiting")

    def test_ae4_silent_after_three_periods_keeps_key_holder_and_ptt_as_last_known(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        f.tick(14.5)
        self.assertEqual(f.d["liveness"], "alive")
        f.tick(1.5)
        d = f.d
        self.assertEqual(d["liveness"], "silent")
        self.assertIn("silent", d["banners"])
        self.assertTrue(d["stale"])
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["ptt"])
        f.live("stato ptt 1")
        self.assertEqual(f.d["liveness"], "alive")

    def test_silence_threshold_follows_the_snapshot_period(self):
        # Three periods of 500 ms: above the one-second floor.
        f = Feed().live(*snap(1, [], period=500))
        f.tick(1.4)
        self.assertEqual(f.d["liveness"], "alive")
        f.tick(0.2)
        self.assertEqual(f.d["liveness"], "silent")

    def test_silence_threshold_never_goes_below_one_second(self):
        # --snapshot-ms 50 gives 150 ms, less than follow.py's 200 ms poll.
        f = Feed().live(*snap(1, [], period=50))
        f.tick(0.5)
        self.assertEqual(f.d["liveness"], "alive")
        f.tick(0.6)
        self.assertEqual(f.d["liveness"], "silent")

    def test_liveness_change_on_tick_increments_the_version(self):
        f = guaranteed_with(C1)
        v = f.m.version
        f.tick(1.0)
        self.assertEqual(f.m.version, v)
        f.tick(20.0)
        self.assertGreater(f.m.version, v)

    def test_stato_arresto_stops_and_later_lines_of_the_instance_do_not_leave_it(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        f.live("stato arresto",
               "stato disconnesso client 1 IU3QEZ da 10.0.0.1:50001: arresto",
               "stato ptt 0")
        d = f.d
        self.assertEqual(d["liveness"], "stopped")
        self.assertFalse(d["ptt"])
        self.assertIn("stopped", d["banners"])
        f.live(*snap(2, [], holder="libera"))
        self.assertEqual(f.d["liveness"], "stopped")
        f.live(ASCOLTO)
        d = f.d
        self.assertEqual(d["liveness"], "alive")
        self.assertFalse(d["guaranteed"])
        self.assertEqual(d["clients"], [])

    def test_snapshot_of_a_new_instance_leaves_stopped(self):
        f = guaranteed_with(C1)
        f.live("stato arresto", *snap(1, [], instance="1789770000-7"))
        self.assertEqual(f.d["liveness"], "alive")
        self.assertTrue(f.d["guaranteed"])

    def test_end_of_input_is_input_closed_once_and_keeps_the_last_state(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        v = f.m.version
        f.m.input_closed(f.now)
        d = f.d
        self.assertEqual(d["liveness"], "input_closed")
        self.assertIn("input_closed", d["banners"])
        self.assertEqual(d["key_holder"], 1)
        self.assertGreater(f.m.version, v)

    def test_lost_events_make_the_state_of_a_live_daemon_stale(self):
        f = guaranteed_with(C1)
        self.assertFalse(f.d["stale"])
        f.live("stato eventi persi 1")
        self.assertTrue(f.d["stale"])

    def test_banners_come_in_the_order_of_trust(self):
        f = Feed().live(*snap(1, [C1], version="v2"), "stato eventi persi 1", "key 1 5")
        f.tick(16.0)
        self.assertEqual(f.d["banners"],
                         ["silent", "not_guaranteed", "mixed_edges", "vocabulary"])


class ViewTest(unittest.TestCase):
    """The values the page copies without deciding anything (#90)."""

    def test_key_view_is_unknown_before_any_line_with_no_holder_and_no_name(self):
        self.assertEqual(Feed().d["key_view"], {"state": "unknown", "idx": None, "name": None})

    def test_key_view_is_free_after_chiave_libera_and_after_a_snapshot_with_the_key_free(self):
        free = {"state": "free", "idx": None, "name": None}
        f = Feed().live("stato chiave client 1 IU3QEZ", "stato chiave libera")
        self.assertEqual(f.d["key_view"], free)
        self.assertEqual(guaranteed_with(C1, holder="libera").d["key_view"], free)

    def test_key_view_is_held_by_the_client_with_its_name_after_a_chiave_line(self):
        f = Feed().live("stato chiave client 2 DL4YHF")
        self.assertEqual(f.d["key_view"], {"state": "held", "idx": 2, "name": "DL4YHF"})

    def test_key_view_has_no_name_when_a_chiave_line_carries_an_empty_one(self):
        # cwnetd writes "(senza nome)" in its place today: the guard is for a
        # later daemon or a hand-written line.
        f = Feed().live("stato chiave client 2 ")
        self.assertEqual(f.d["key_view"], {"state": "held", "idx": 2, "name": None})
        self.assertEqual(f.d["key_holder_name"], "")

    def test_key_view_has_no_name_when_the_snapshot_holder_is_missing_from_its_clients(self):
        f = Feed().live(*snap(1, [C1], holder="2"))
        self.assertEqual(f.d["key_view"], {"state": "held", "idx": 2, "name": None})

    def test_key_and_ptt_views_go_back_to_unknown_after_a_new_daemon_instance(self):
        f = guaranteed_with(C1, holder="1", ptt=1)
        self.assertEqual(f.d["key_view"]["state"], "held")
        self.assertEqual(f.d["ptt_view"], "on")
        f.live(ASCOLTO)
        self.assertEqual(f.d["key_view"]["state"], "unknown")
        self.assertEqual(f.d["ptt_view"], "unknown")

    def test_ptt_view_is_unknown_before_any_line_then_follows_the_ptt_lines(self):
        f = Feed()
        self.assertEqual(f.d["ptt_view"], "unknown")
        f.live("stato ptt 1")
        self.assertEqual(f.d["ptt_view"], "on")
        f.live("stato ptt 0")
        self.assertEqual(f.d["ptt_view"], "off")

    def test_ptt_view_follows_the_ptt_field_of_the_snapshot(self):
        f = Feed().live(*snap(1, [C1], ptt=1))
        self.assertEqual(f.d["ptt_view"], "on")
        f.live(*snap(2, [C1], ptt=0))
        self.assertEqual(f.d["ptt_view"], "off")

    def test_stale_is_false_only_when_alive_and_guaranteed(self):
        self.assertFalse(guaranteed_with(C1).d["stale"])
        alive_not_guaranteed = Feed().live("stato ptt 0")
        self.assertEqual(alive_not_guaranteed.d["liveness"], "alive")
        self.assertTrue(alive_not_guaranteed.d["stale"])

    def test_stale_while_waiting_silent_stopped_and_input_closed_with_a_guaranteed_state(self):
        waiting = Feed().backlog(*snap(1, [C1]))
        silent = guaranteed_with(C1).tick(16.0)
        stopped = guaranteed_with(C1).live("stato arresto")
        closed = guaranteed_with(C1)
        closed.m.input_closed(closed.now)
        for f in (waiting, silent, stopped, closed):
            with self.subTest(liveness=f.d["liveness"]):
                self.assertTrue(f.d["guaranteed"])
                self.assertTrue(f.d["stale"])

    def test_stale_after_a_confession_until_the_next_complete_snapshot(self):
        f = guaranteed_with(C1)
        f.live("stato stdout 1 righe scartate")
        self.assertTrue(f.d["stale"])
        f.live("stato ptt 0")
        self.assertTrue(f.d["stale"], "a level line does not restore the guarantee")
        f.live(*snap(2, [C1]))
        self.assertFalse(f.d["stale"])

    def test_fitness_is_unknown_for_a_client_with_no_peak(self):
        f = guaranteed_with((1, "pronto", "10.0.0.1:50001", -1, -1, "IU3QEZ"))
        self.assertEqual(f.d["clients"][0]["fitness"], "unknown")

    def test_fitness_is_unknown_for_a_measured_client_before_the_ceiling_is_known(self):
        # The page showed this client as "idoneo": the link ceiling comes with
        # "stato ascolto" or the first snapshot, and neither has arrived.
        f = Feed().live("stato accettato client 1 da 10.0.0.1:50001",
                        "stato latenza client 1 12 ms peak 15 ms")
        self.assertIsNone(f.d["settings"]["link_ceiling_ms"])
        self.assertEqual(f.d["clients"][0]["peak_ms"], 15)
        self.assertEqual(f.d["clients"][0]["fitness"], "unknown")

    def test_fitness_is_fit_below_and_at_the_ceiling_and_unfit_one_millisecond_over(self):
        # take_key() in cwnet_server.c refuses a peak over the ceiling, not
        # one equal to it.
        f = guaranteed_with(*[(idx, "pronto", "10.0.0.%d:5000" % idx, 10, peak, "C%d" % idx)
                              for idx, peak in ((1, 999), (2, 1000), (3, 1001))])
        self.assertEqual(f.d["settings"]["link_ceiling_ms"], 1000)
        self.assertEqual([c["fitness"] for c in f.d["clients"]], ["fit", "fit", "unfit"])

    def test_event_read_back_from_the_file_says_so_and_has_no_time_and_a_live_one_neither(self):
        line = "stato fault: over oltre il tetto"
        f = Feed().backlog(line)
        self.assertTrue(f.d["events"][-1]["from_file"])
        self.assertIsNone(f.d["events"][-1]["t"])
        f.live(line)
        self.assertFalse(f.d["events"][-1]["from_file"])
        self.assertIsNotNone(f.d["events"][-1]["t"])


class AcceptanceTest(unittest.TestCase):

    def test_ae1_lost_key_and_ptt_lines_are_declared_then_corrected_by_the_snapshot(self):
        f = guaranteed_with(C1, holder="libera", ptt=0)
        # Lost: "stato over ...", "stato chiave client 1 IU3QEZ", "stato ptt 1".
        f.live("stato stdout 3 righe scartate", "stato latenza client 1 12 ms peak 15 ms")
        d = f.d
        self.assertFalse(d["guaranteed"])
        self.assertIn("not_guaranteed", d["banners"])
        self.assertIsNone(d["key_holder"])
        f.live(*snap(2, [C1], holder="1", ptt=1))
        d = f.d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["ptt"])

    def test_ae2_start_without_ascolto_converges_at_the_first_complete_snapshot(self):
        f = Feed().backlog("ms tetto 1000 ms coda 100 ms lead 0 ms out-cap 16384 byte",
                           "stato chiave client 1 IU3QEZ")
        f.live("stato ptt 1", *snap(9, [C1, C2], holder="1", ptt=1))
        d = f.d
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["settings"]["link_ceiling_ms"], 1000)
        self.assertEqual(len(d["clients"]), 2)
        self.assertEqual(d["key_holder"], 1)
        self.assertTrue(d["ptt"])


class RealDaemonLinesTest(unittest.TestCase):
    """Lines as the built cwnetd wrote them: three U1 hand runs, 2026-09-19, one
    edges path shortened. Two instances, so the second snapshot of 39046 is a
    new daemon seen without its `stato ascolto`."""

    LINES = [
        "stato ascolto 127.0.0.1:17500 max-clients 4 B>=100 ms tetto 1000 ms coda 100 ms "
        "lead 0 ms out-cap 16384 byte",
        "stato uscita virtual fronti /var/folders/cc/T/tmpml_vm_ww/edges.log",
        "stato snap inizio v1 istanza 1789769781-39043 seq 1 client 0 chiave libera ptt 0 "
        "periodo 250",
        "stato snap manopole max-clients 4 B>= 100 tetto 1000 coda 100 lead 0 idle 5000 "
        "over-max 120000 handshake 5000 out-cap 16384",
        "stato snap fine seq 1",
        "stato accettato client 1 da 127.0.0.1:60412",
        "stato connesso client 1 A1 da 127.0.0.1:60412",
        "stato accettato client 2 da 127.0.0.1:60413",
        "stato connesso client 2 B2 <b>x</b> da 127.0.0.1:60413",
        "stato snap inizio v1 istanza 1789769781-39043 seq 2 client 2 chiave libera ptt 0 "
        "periodo 250",
        "stato snap manopole max-clients 4 B>= 100 tetto 1000 coda 100 lead 0 idle 5000 "
        "over-max 120000 handshake 5000 out-cap 16384",
        "stato snap client 1/2 1 pronto 127.0.0.1:60412 lat -1 peak -1 nome 2 A1",
        "stato snap client 2/2 2 pronto 127.0.0.1:60413 lat -1 peak -1 nome 11 B2 <b>x</b>",
        "stato snap fine seq 2",
        "stato over client 1 A1 B 100 ms",
        "stato chiave client 1 A1",
        "stato ptt 1",
        "stato latenza client 1 1 ms peak 1 ms",
        "stato disconnesso client 2 B2 <b>x</b> da 127.0.0.1:60413: chiuso dal peer",
        "stato disconnesso client 1 A1 da 127.0.0.1:60412: chiuso dal peer",
        "stato fault client 1 A1: titolare sparito a meta' over",
        "stato chiave libera",
        "stato ptt 0",
        "stato snap inizio v1 istanza 1789769781-39043 seq 3 client 1 chiave libera ptt 0 "
        "periodo 250",
        "stato snap manopole max-clients 4 B>= 100 tetto 1000 coda 100 lead 0 idle 5000 "
        "over-max 120000 handshake 5000 out-cap 16384",
        "stato snap client 1/1 1 attesa 127.0.0.1:60415 lat -1 peak -1 nome 0 ",
        "stato snap fine seq 3",
        "stato snap inizio v1 istanza 1789769784-39046 seq 2 client 1 chiave libera ptt 0 "
        "periodo 500",
        "stato snap manopole max-clients 4 B>= 100 tetto 1000 coda 100 lead 0 idle 5000 "
        "over-max 120000 handshake 5000 out-cap 16384",
        "stato snap client 1/1 1 pronto 127.0.0.1:60414 lat -1 peak -1 nome 173 X" + "\\x1B" * 43,
        "stato snap fine seq 2",
        "stato arresto",
    ]

    def test_every_line_of_a_real_run_is_recognised(self):
        for line in self.LINES:
            for p in state.parse(line):
                self.assertNotIn(p.kind, ("unknown", "unreadable", "ignored"), line)

    def test_a_real_run_ends_guaranteed_on_the_last_snapshot_and_stopped(self):
        f = Feed().live(*self.LINES)
        d = f.d
        self.assertEqual(d["liveness"], "stopped")
        self.assertTrue(d["guaranteed"])
        self.assertEqual(d["instance"], "1789769784-39046")
        self.assertEqual(d["clients"][0]["name"], "X" + "\\x1B" * 43)
        self.assertFalse(d["clients"][0]["name_truncated"])
        self.assertNotIn("/var/folders", json.dumps(d))


if __name__ == "__main__":
    unittest.main()
