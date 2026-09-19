#!/usr/bin/env python3
"""Tests for follow.py: complete lines out of a file followed by name and out of stdin.

The file tests drive FileFollower.step() by hand, one call per poll, so what
they check does not depend on the 200 ms clock. The writers use the same
file modes as the operator's setup: 'ab' is `cwnetd >> file`, 'wb' is
`cwnetd > file`, os.truncate() is what logrotate's copytruncate does to the
original, os.rename() then a new file is a daemon restarted onto a new file.

Run from the repository root:
    python3 -m unittest discover -s host/panel/tests -t host/panel -v
"""
import os
import tempfile
import threading
import unittest

import follow
from follow import Line


def live(*texts):
    return [Line(t, False) for t in texts]


def backlog(*texts):
    return [Line(t, True) for t in texts]


class FileFollowerTest(unittest.TestCase):

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = os.path.join(tmp.name, "cwnetd.log")

    def follower(self, **kwargs):
        f = follow.FileFollower(self.path, **kwargs)
        self.addCleanup(f.close)
        return f

    def append(self, data, path=None):
        with open(path or self.path, "ab") as fh:
            fh.write(data)

    def writer(self, mode="ab"):
        # Unbuffered, so every write() reaches the file before the next step().
        fh = open(self.path, mode, buffering=0)
        self.addCleanup(fh.close)
        return fh

    # --- first opening -------------------------------------------------

    def test_existing_200k_file_yields_only_lines_inside_last_64k_marked_backlog(self):
        line_len = 100
        lines = [("stato riga %06d " % i).encode().ljust(line_len - 1, b"x") + b"\n"
                 for i in range(2048)]
        data = b"".join(lines)
        self.assertEqual(len(data), 200 * 1024)
        self.append(data)
        window_start = len(data) - follow.BACKLOG_BYTES
        # The window must start inside a line, or there is no partial line to drop.
        self.assertNotEqual(window_start % line_len, 0)
        cut_line = lines[window_start // line_len][:-1].decode()
        expected = [ln[:-1].decode() for i, ln in enumerate(lines)
                    if i * line_len >= window_start]

        f = self.follower()
        got = f.step()

        self.assertEqual(got, backlog(*expected))
        self.assertNotIn(cut_line, [ln.text for ln in got])
        self.assertTrue(all(ln.text.startswith("stato riga ") for ln in got))
        self.append(b"stato dopo l'apertura\n")
        self.assertEqual(f.step(), live("stato dopo l'apertura"))

    def test_backlog_window_starting_exactly_on_a_line_keeps_that_line(self):
        # 50 bytes, window of 20 starts at offset 30: the first byte of "bbb...".
        self.append(b"aaaaaaaaa\n" * 3 + b"bbbbbbbbb\nccccccccc\n")
        f = self.follower(backlog_bytes=20)
        self.assertEqual(f.step(), backlog("bbbbbbbbb", "ccccccccc"))

    def test_existing_file_smaller_than_window_is_read_whole_as_backlog(self):
        self.append(b"stato ascolto\nstato connesso client 1\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto", "stato connesso client 1"))

    def test_line_incomplete_at_opening_is_live_when_its_newline_arrives(self):
        self.append(b"stato ascolto\nstato chi")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto"))
        self.append(b"ave client 1\n")
        self.assertEqual(f.step(), live("stato chiave client 1"))

    def test_missing_file_yields_nothing_until_created_then_is_read_from_start_as_live(self):
        f = self.follower(backlog_bytes=10)
        self.assertEqual(f.step(), [])
        self.assertEqual(f.step(), [])
        # Larger than the window: the window applies only to a file already
        # there at the first step, not to one the daemon creates later.
        self.append(b"stato ascolto 0123456\nstato connesso 12345\n")
        self.assertEqual(f.step(), live("stato ascolto 0123456", "stato connesso 12345"))

    # --- following -----------------------------------------------------

    def test_line_written_in_two_halves_is_delivered_once_at_the_newline(self):
        self.append(b"")
        f = self.follower()
        self.assertEqual(f.step(), [])
        self.append(b"stato chiave cl")
        self.assertEqual(f.step(), [])
        self.append(b"ient 1\n")
        self.assertEqual(f.step(), live("stato chiave client 1"))
        self.assertEqual(f.step(), [])

    def test_truncation_to_zero_restarts_from_start_without_loss_or_duplicates(self):
        self.append(b"stato riga uno prima della rotazione\n"
                    b"stato riga due prima della rotazione\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato riga uno prima della rotazione",
                                           "stato riga due prima della rotazione"))
        # Half a line held when copytruncate empties the file: its end is gone
        # with the old content and must not be glued to the first new line.
        self.append(b"stato mez")
        self.assertEqual(f.step(), [])
        os.truncate(self.path, 0)
        self.append(b"stato a\nstato b\n")
        self.assertEqual(f.step(), live("stato a", "stato b"))
        self.append(b"stato c\n")
        self.assertEqual(f.step(), live("stato c"))
        self.assertEqual(f.step(), [])

    def test_restart_onto_new_file_after_rename_delivers_old_tail_then_new_file(self):
        old = self.writer()
        old.write(b"stato ascolto vecchio\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto vecchio"))
        old.write(b"stato chiave client 1\n")
        os.rename(self.path, self.path + ".1")
        # The old daemon writes its last lines into the renamed file and is
        # killed in the middle of one.
        old.write(b"stato arresto\nstato pt")
        old.close()
        self.append(b"stato ascolto nuovo\n")
        self.assertEqual(f.step(), live("stato chiave client 1", "stato arresto",
                                        "stato ascolto nuovo"))
        self.append(b"stato connesso client 1\n")
        self.assertEqual(f.step(), live("stato connesso client 1"))

    def test_renamed_file_is_still_read_while_no_new_file_exists(self):
        old = self.writer()
        old.write(b"stato ascolto\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto"))
        os.rename(self.path, self.path + ".1")
        old.write(b"stato chiave client 2\n")
        self.assertEqual(f.step(), live("stato chiave client 2"))
        old.write(b"stato arresto\n")
        old.close()
        self.assertEqual(f.step(), live("stato arresto"))
        self.append(b"stato ascolto\n")
        self.assertEqual(f.step(), live("stato ascolto"))

    def test_block_of_nuls_is_skipped_and_following_lines_arrive(self):
        self.append(b"stato ascolto\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto"))
        self.append(b"\0" * 8192 + b"stato chiave client 1\nstato ptt 1\n")
        self.assertEqual(f.step(), live("stato chiave client 1", "stato ptt 1"))

    def test_hole_left_by_writer_without_append_after_copytruncate_is_skipped(self):
        # `cwnetd > file`: the writer keeps its own offset, so after the
        # truncation its next write lands past the new end of file and the
        # gap in front of it reads as NUL bytes.
        w = self.writer("wb")
        w.write(b"stato ascolto\nstato connesso client 1\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto", "stato connesso client 1"))
        os.truncate(self.path, 0)
        self.assertEqual(f.step(), [])
        w.write(b"stato chiave client 1\n")
        self.assertGreater(os.path.getsize(self.path), len(b"stato chiave client 1\n"))
        self.assertEqual(f.step(), live("stato chiave client 1"))

    def test_non_utf8_bytes_become_replacement_chars_and_next_line_is_intact(self):
        self.append(b"")
        f = self.follower()
        self.assertEqual(f.step(), [])
        self.append(b"stato connesso client 1 \xff\xfe\xc3\nstato ascolto\n")
        self.assertEqual(f.step(), live("stato connesso client 1 ���",
                                        "stato ascolto"))

    def test_utf8_character_split_across_two_reads_is_decoded_whole(self):
        self.append(b"")
        f = self.follower()
        self.assertEqual(f.step(), [])
        self.append(b"stato nome \xc3")
        self.assertEqual(f.step(), [])
        self.append(b"\xa8\n")
        self.assertEqual(f.step(), live("stato nome è"))

    def test_only_the_newline_is_removed_from_a_line(self):
        self.append(b"")
        f = self.follower()
        self.assertEqual(f.step(), [])
        self.append(b" stato ascolto \r\n")
        self.assertEqual(f.step(), live(" stato ascolto \r"))

    def test_line_longer_than_cap_is_dropped_whole_and_next_line_arrives(self):
        self.append(b"")
        f = self.follower()
        self.assertEqual(f.step(), [])
        # Held without a newline past the cap: dropped, and so is the rest of it.
        self.append(b"x" * (follow.MAX_LINE_BYTES + 1))
        self.assertEqual(f.step(), [])
        self.append(b"rest of the long line\nstato ascolto\n")
        self.assertEqual(f.step(), live("stato ascolto"))
        # The same line arriving whole in one read gets the same verdict.
        self.append(b"y" * (follow.MAX_LINE_BYTES + 1) + b"\nstato arresto\n")
        self.assertEqual(f.step(), live("stato arresto"))

    def test_step_after_close_reads_nothing(self):
        self.append(b"stato ascolto\n")
        f = self.follower()
        self.assertEqual(f.step(), backlog("stato ascolto"))
        f.close()
        self.append(b"stato arresto\n")
        self.assertEqual(f.step(), [])

    # --- thread target -------------------------------------------------

    def test_follow_file_delivers_only_nonempty_batches_and_stops_on_event(self):
        self.append(b"stato ascolto\n")
        cond = threading.Condition()
        batches = []

        def on_lines(lines):
            with cond:
                batches.append(list(lines))
                cond.notify_all()

        stop = threading.Event()
        t = threading.Thread(target=follow.follow_file,
                             args=(self.path, on_lines, stop, 0.01), daemon=True)
        t.start()
        try:
            with cond:
                self.assertTrue(cond.wait_for(lambda: len(batches) >= 1, timeout=5))
            # About ten empty polls: none of them may reach on_lines.
            stop.wait(0.1)
            self.append(b"stato arresto\n")
            with cond:
                self.assertTrue(cond.wait_for(lambda: len(batches) >= 2, timeout=5))
        finally:
            stop.set()
            t.join(timeout=5)
        self.assertFalse(t.is_alive())
        self.assertEqual(batches, [backlog("stato ascolto"), live("stato arresto")])


class ReadStreamTest(unittest.TestCase):

    def pipe(self):
        r, w = os.pipe()
        # A BufferedReader, the same type as sys.stdin.buffer.
        reader = os.fdopen(r, "rb")
        writer = os.fdopen(w, "wb", buffering=0)
        self.addCleanup(reader.close)
        self.addCleanup(writer.close)
        return reader, writer

    def test_closed_stdin_signals_eof_once_and_drops_held_half_line(self):
        reader, writer = self.pipe()
        writer.write(b"stato ascolto\nstato chiave client 1\nstato mez")
        writer.close()
        batches, eofs = [], []
        follow.read_stream(reader, batches.append, lambda: eofs.append(1))
        self.assertEqual(eofs, [1])
        self.assertTrue(all(batches))
        self.assertEqual([ln for b in batches for ln in b],
                         live("stato ascolto", "stato chiave client 1"))

    def test_stdin_closed_without_data_signals_eof_once_and_no_lines(self):
        reader, writer = self.pipe()
        writer.close()
        batches, eofs = [], []
        follow.read_stream(reader, batches.append, lambda: eofs.append(1))
        self.assertEqual(eofs, [1])
        self.assertEqual(batches, [])

    def test_stdin_line_is_delivered_at_its_newline_while_writer_stays_open(self):
        reader, writer = self.pipe()
        cond = threading.Condition()
        batches, eofs = [], []

        def on_lines(lines):
            with cond:
                batches.append(list(lines))
                cond.notify_all()

        def on_eof():
            with cond:
                eofs.append(1)
                cond.notify_all()

        t = threading.Thread(target=follow.read_stream, args=(reader, on_lines, on_eof),
                             daemon=True)
        t.start()
        writer.write(b"stato ascolto\n")
        with cond:
            # bool: wait_for() returns the predicate's value, and the list
            # itself would fill up after the writer closes.
            delivered = cond.wait_for(lambda: len(batches) > 0, timeout=5)
            eof_before_close = list(eofs)
        writer.close()
        t.join(timeout=5)
        self.assertTrue(delivered, "line not delivered while the writer was still open")
        self.assertEqual(eof_before_close, [])
        self.assertEqual(batches, [live("stato ascolto")])
        self.assertEqual(eofs, [1])
        self.assertFalse(t.is_alive())


if __name__ == "__main__":
    unittest.main()
