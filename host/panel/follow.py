#!/usr/bin/env python3
"""Input side of the station panel: complete `cwnetd` status lines out of stdin
or out of a file followed by name.

Sources
-------
The operator runs the daemon in one of three ways (KTD10, R15):

    cwnetd >> /var/log/cwnetd.log                  -> follow_file(path, ...)
    journalctl -f -n 0 -o cat -u cwnetd | panel    -> read_stream(stdin, ...)
    cwnetd | panel                                 -> read_stream(stdin, ...)

Both paths deliver lists of Line: the text of one complete line, decoded with
errors='replace' and without its '\\n', and whether it was already in the file
when the panel first opened it (backlog). Backlog lines count for the state,
not for liveness (KTD9). A line is delivered only at its newline: half a
status line parses as a wrong but plausible line.

Following a file by name
------------------------
Like `tail -F`, by polling every 200 ms: no inotify, so the same code runs on
Linux and macOS. FileFollower.step() is one poll and never sleeps, so the
tests call it by hand instead of racing a clock.

- First opening: the last 64 KiB, first partial line discarded.
- Size below the read position: the file was truncated (copytruncate),
  read again from offset 0.
- A different (st_dev, st_ino) at the path: a daemon restarted onto a new
  file after the old one was renamed. The old file is read to its end, then
  the new one from offset 0.
- NUL bytes are dropped. With `cwnetd > file` (no O_APPEND) the daemon keeps
  its own offset across a copytruncate, and the gap up to it reads as NULs.
  The daemon escapes every non-printable byte as \\xNN, so a raw NUL is never
  part of one of its lines.

Rotation by rename is not followed on purpose: `cwnetd` never reopens stdout,
so it keeps writing into the renamed file while the path gets a new empty
one. That is why the README asks for copytruncate.
"""
import os
import typing
from typing import Callable, List, Optional, Tuple

POLL_INTERVAL_S = 0.2
BACKLOG_BYTES = 64 * 1024
# A daemon line is at most 255 bytes plus '\n'. Anything longer is not one of
# its lines, and holding it would let a writer that never sends '\n' grow the
# buffer without bound.
MAX_LINE_BYTES = 64 * 1024
READ_CHUNK = 64 * 1024


class Line(typing.NamedTuple):
    text: str       # decoded with errors='replace', trailing '\n' removed, nothing else stripped
    backlog: bool   # True only for lines already in the file when the panel first opened it


class _LineSplitter:
    """Bytes in, complete lines out; holds back the bytes after the last '\\n'."""

    def __init__(self):
        self._held = bytearray()
        # True while inside a line that is being thrown away: the first
        # partial line of the backlog window, or a line past MAX_LINE_BYTES.
        # Its tail must not come out as a line of its own.
        self._skipping = False

    def reset(self, skip_first_line=False):
        self._held.clear()
        self._skipping = skip_first_line

    def feed(self, data, backlog):
        data = data.replace(b"\0", b"")
        out = []
        start = 0
        while True:
            nl = data.find(b"\n", start)
            if nl < 0:
                break
            piece = data[start:nl]
            start = nl + 1
            if self._skipping:
                self._skipping = False
                continue
            if len(self._held) + len(piece) <= MAX_LINE_BYTES:
                # Decoded per line, after the line is whole: a UTF-8 sequence
                # split between two reads is decoded as one character, and a
                # bad byte cannot reach into the next line.
                out.append(Line((self._held + piece).decode("utf-8", "replace"), backlog))
            self._held.clear()
        if not self._skipping:
            self._held += data[start:]
            if len(self._held) > MAX_LINE_BYTES:
                self._held.clear()
                self._skipping = True
        return out


class FileFollower:
    """Follows one path by name, one poll per step()."""

    def __init__(self, path: str, backlog_bytes: int = BACKLOG_BYTES):
        self.path = path
        self.backlog_bytes = backlog_bytes
        self._fd: Optional[int] = None
        self._ident: Optional[Tuple[int, int]] = None
        # Offset of the next byte to read. Kept here and read with pread(),
        # so the descriptor's own offset plays no part.
        self._pos = 0
        # Bytes before this offset were in the file at the first opening.
        self._backlog_end = 0
        self._tried_once = False
        self._closed = False
        self._splitter = _LineSplitter()

    def step(self) -> List[Line]:
        """One poll: the lines completed since the previous one, in file order."""
        if self._closed:
            # Opening again would start from offset 0 as live lines and
            # replay the whole file into the model.
            return []
        lines: List[Line] = []
        if self._fd is None:
            if not self._open():
                return lines
        else:
            ident = self._path_ident()
            if ident is not None and ident != self._ident:
                # The old daemon may have written its last lines (the
                # shutdown, the PTT release) after our previous read.
                lines += self._read_to_eof()
                self._close_fd()
                if not self._open():
                    return lines
            elif os.fstat(self._fd).st_size < self._pos:
                self._pos = 0
                self._backlog_end = 0
                # The rest of the held line went away with the old content.
                self._splitter.reset()
        lines += self._read_to_eof()
        return lines

    def close(self) -> None:
        self._closed = True
        self._close_fd()

    def _open(self):
        first = not self._tried_once
        self._tried_once = True
        try:
            fd = os.open(self.path, os.O_RDONLY)
        except FileNotFoundError:
            return False
        st = os.fstat(fd)
        self._fd = fd
        self._ident = (st.st_dev, st.st_ino)
        self._backlog_end = st.st_size if first else 0
        if first and st.st_size > self.backlog_bytes:
            # Start one byte before the window and skip through the first
            # '\n'. When that byte is itself a '\n', the window starts on a
            # line and only that byte is skipped, so the line is kept.
            self._pos = st.st_size - self.backlog_bytes - 1
            self._splitter.reset(skip_first_line=True)
        else:
            # A file that appears after the first step was started by the
            # daemon after the panel: everything in it is live.
            self._pos = 0
            self._splitter.reset()
        return True

    def _close_fd(self):
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
            self._ident = None

    def _path_ident(self):
        try:
            st = os.stat(self.path)
        except FileNotFoundError:
            # Renamed and no new file yet: the old daemon may still be
            # writing into the descriptor we hold.
            return None
        return (st.st_dev, st.st_ino)

    def _read_to_eof(self):
        lines = []
        while True:
            data = os.pread(self._fd, READ_CHUNK, self._pos)
            if not data:
                return lines
            start = self._pos
            self._pos += len(data)
            if start < self._backlog_end:
                cut = min(len(data), self._backlog_end - start)
                lines += self._splitter.feed(data[:cut], True)
                data = data[cut:]
            if data:
                # A line held across the backlog boundary ends here, so it
                # was not complete at opening: it is live.
                lines += self._splitter.feed(data, False)


def follow_file(path: str,
                on_lines: Callable[[List[Line]], None],
                stop_event,
                interval: float = POLL_INTERVAL_S) -> None:
    """Thread target: calls FileFollower.step() every `interval` s until stop_event is set;
    calls on_lines(list_of_Line) only with non-empty batches."""
    follower = FileFollower(path)
    try:
        while not stop_event.is_set():
            lines = follower.step()
            if lines:
                on_lines(lines)
            stop_event.wait(interval)
    finally:
        follower.close()


def read_stream(stream, on_lines: Callable[[List[Line]], None],
                on_eof: Callable[[], None]) -> None:
    """Thread target for stdin: `stream` is a binary file object (sys.stdin.buffer) or anything
    with a fileno(). Delivers complete lines as they arrive (backlog=False), without waiting for a
    buffer to fill; at EOF calls on_eof() exactly once and returns."""
    # os.read() returns what the pipe holds. A buffered read(n) waits for n
    # bytes, and the journal is quiet exactly between events.
    fd = stream.fileno()
    splitter = _LineSplitter()
    try:
        while True:
            data = os.read(fd, READ_CHUNK)
            if not data:
                # A held half line is dropped: the daemon ends every line
                # with '\n', so what is held here was cut off.
                return
            lines = splitter.feed(data, False)
            if lines:
                on_lines(lines)
    finally:
        # Also on a read error: no more input will arrive either way, and the
        # model must be able to say so.
        on_eof()
