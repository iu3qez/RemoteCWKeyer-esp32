#!/usr/bin/env python3
"""Client CWNet di prova: connette, risponde ai PING, manda keying, legge.

Serve il verso opposto a cwnet_echo.py. L'echo e' un server finto per provare
la scatola; questo e' un client finto per provare il daemon vero, `cwnetd`,
senza tirare fuori la scatola dal cassetto (U6, "loop senza scatola").

Fa il minimo che un client deve fare perche' il server lo consideri uno che
puo' trasmettere:

  * CONNECT di 92 byte con username e nominativo, e attesa dell'eco
  * RESPONSE_1 a ogni PING REQUEST del server, che e' l'iniziatore
  * invio dei byte grezzi di una fixture della cattura del 2026-09-05, o di
    un file, cosi' come sono: sono gia' frame CWNet completi
  * stampa di quello che torna, con TX_INFO e RPRT in chiaro

I byte non vengono ritemporizzati: il server li riproduce con le attese che
portano dentro, ed e' proprio quella riproduzione che si sta guardando.

  python3 cwnet_send.py --fixture first_over
  python3 cwnet_send.py --host 192.168.1.10 --call IU3QEZ --file over.bin
"""
import argparse
import socket
import sys
import time

from cwnet_wire import (CMD_CONNECT, CMD_DISCONNECT, CMD_MORSE, CMD_PING,
                        CMD_PRINT, CMD_RIG, CMD_TX_INFO, NAMES, FrameParser,
                        decode7, encode7, frame, le32)

CONNECT_FIELD_LEN = 44          # username e nominativo, a lunghezza fissa
CONNECT_PAYLOAD_LEN = 92        # 44 + 44 + 4 di permessi

# Byte di client_to_server della cattura del 2026-09-05, gli stessi di
# test_host/cwnet_fixtures.h. Frame interi, non payload: si spediscono cosi'.
FIXTURES = {
    # ref_first_over: sessione 12, la lettera A a 25 WPM (dot 48 ms), con le
    # due stringhe set_ptt che il riferimento intercala all'over.
    "first_over": bytes([
        0x50, 0x01, 0x80,
        0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74, 0x20, 0x31, 0x0A, 0x00,
        0x50, 0x01, 0x24,
        0x50, 0x01, 0xA4,
        0x50, 0x01, 0x3C,
        0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74, 0x20, 0x30, 0x0A, 0x00,
        0x50, 0x01, 0x60,
    ]),
    # ref_two_event_frames: due frame da due eventi ciascuno, dot circa 20 ms.
    "two_event": bytes([
        0x50, 0x02, 0x96, 0x12,
        0x50, 0x02, 0x96, 0x12,
    ]),
    # synth_morse_frame: 'S' 'O' 'S' generato con l'encoder DL4YHF da
    # tools/cwnet/gen_synth.c, con il key-up di fine over in coda.
    "synth": bytes([
        0x50, 0x11,
        0x80, 0x41, 0xA7, 0x27, 0xC1, 0x27, 0xA7, 0x41, 0xC1, 0x27, 0xC1, 0x27,
        0xA7, 0x27, 0xC1, 0x55, 0x00,
    ]),
    # Solo il primo key-down: la FIFO resta a secco e il server deve andare in
    # underrun invece di lasciare il tasto giu' (AE6).
    "key_down_only": bytes([0x50, 0x01, 0x80]),
}


def build_long_sequence(reps=13, dot_ms=48):
    """~100 elementi (reps*8) per la misura del jitter (U7): la lettera 'V'
    (...-) ripetuta, dot/spazio-intra a dot_ms, tratto/spazio-lettera a
    3*dot_ms. Ogni evento alterna stato rispetto al precedente -- nessuna
    coppia di key-up consecutivi, cosi' ogni byte mandato produce esattamente
    un fronte sull'uscita virtuale e il conteggio si verifica a vista.

    Ritorna il frame CWNet completo (comando + lunghezza + payload)."""
    unit = [dot_ms, dot_ms, dot_ms, dot_ms, dot_ms, dot_ms, 3 * dot_ms, 3 * dot_ms]
    durations = unit * reps
    down = True
    payload = bytearray([0x80])  # primo evento: key down, delay 0
    for d in durations[:-1]:
        down = not down
        payload.append((0x80 if down else 0x00) | encode7(d))
    return frame(CMD_MORSE, bytes(payload))


# "long": 104 elementi, per cwnet_jitter.py (U7, Success Criteria KTD3). Non
# nel dict letterale sopra perche' si costruisce con encode7()/frame(), non
# ancora definite li'.
FIXTURES["long"] = build_long_sequence()


def now_ms():
    """Millisecondi monotoni sui 31 bit del filo."""
    return int(time.monotonic() * 1000) & 0x7FFFFFFF


def field(text):
    b = text.encode("ascii", "replace")[:CONNECT_FIELD_LEN - 1]
    return b + b"\0" * (CONNECT_FIELD_LEN - len(b))


def connect_payload(user, call, permissions):
    return field(user) + field(call) + le32(permissions)


def safe(b):
    """Come il daemon: quello che arriva dal peer non finisce grezzo a schermo."""
    out = []
    for c in b:
        if c == 0x5C:
            out.append("\\\\")
        elif 0x20 <= c < 0x7F:
            out.append(chr(c))
        else:
            out.append("\\x%02X" % c)
    return "".join(out)


class Sender:
    def __init__(self, cfg, payload):
        self.cfg = cfg
        self.payload = payload
        self.parser = FrameParser()
        self.sock = None
        self.ready = False
        self.sent = False
        self.t_ready = None
        self.t_sent = None

    def log(self, msg):
        print("[%.3f] %s" % (time.monotonic(), msg), flush=True)

    def run(self):
        self.sock = socket.create_connection((self.cfg.host, self.cfg.port), timeout=5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.log("connesso a %s:%d" % (self.cfg.host, self.cfg.port))

        payload = connect_payload(self.cfg.user, self.cfg.call, self.cfg.permissions)
        self.sock.sendall(frame(CMD_CONNECT, payload))
        self.log("-> CONNECT user=%r call=%r" % (self.cfg.user, self.cfg.call))

        deadline = time.monotonic() + self.cfg.hold
        self.sock.settimeout(0.1)
        while time.monotonic() < deadline:
            if self.ready and not self.sent and time.monotonic() >= self.t_ready + self.cfg.delay:
                self.send_payload()
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                self.log("il server ha chiuso")
                return 0
            for code, pl in self.parser.feed(data):
                self.on_frame(code, pl)
        self.log("fine dell'attesa (--hold %g s)" % self.cfg.hold)
        try:
            self.sock.sendall(frame(CMD_DISCONNECT))
        except OSError:
            pass
        return 0

    def send_payload(self):
        self.sent = True
        self.t_sent = time.monotonic()
        if self.cfg.verbose:
            for code, pl in FrameParser().feed(self.payload):
                if code == CMD_MORSE:
                    ev = " ".join("%s%d" % ("D" if b & 0x80 else "u", decode7(b)) for b in pl)
                    self.log("-> MORSE %d byte: %s" % (len(pl), ev))
                else:
                    self.log("-> 0x%02X %s %d byte" % (code, NAMES.get(code, "?"), len(pl)))
        self.sock.sendall(self.payload)
        self.log("-> %d byte di keying inviati" % len(self.payload))

    def on_frame(self, code, payload):
        if code == CMD_CONNECT:
            perm = int.from_bytes(payload[88:92], "little") if len(payload) >= 92 else 0
            self.log("<- CONNECT echo, permessi 0x%02X%s" %
                     (perm, "" if perm & 0x02 else "  (senza TRANSMIT!)"))
            if not self.ready:
                self.ready = True
                self.t_ready = time.monotonic()
        elif code == CMD_PING and len(payload) >= 16:
            kind, pid = payload[0], payload[1]
            if kind == 0:
                # RESPONSE_1: id e t0 come sono arrivati, t1 dal nostro orologio
                resp = bytes([1, pid, 0, 0]) + payload[4:8] + le32(now_ms()) + bytes(4)
                self.sock.sendall(frame(CMD_PING, resp))
                if self.cfg.verbose:
                    self.log("<- PING request id=%d, -> response 1" % pid)
            elif kind == 2:
                t0 = int.from_bytes(payload[4:8], "little")
                t2 = int.from_bytes(payload[12:16], "little")
                self.log("<- PING response 2 id=%d: il server misura %d ms" % (pid, t2 - t0))
        elif code == CMD_TX_INFO and payload:
            idx = payload[0]
            name = safe(payload[1:].split(b"\0", 1)[0])
            who = "nessuno" if idx == 0xFF else "client %d" % idx
            self.log("<- TX_INFO: %s (%s)" % (who, name))
        elif code == CMD_RIG:
            self.log("<- RIG %r" % safe(payload.split(b"\0", 1)[0]))
        elif code == CMD_PRINT:
            self.log("<- PRINT %r" % safe(payload.split(b"\0", 1)[0]))
        else:
            self.log("<- 0x%02X %s %d byte" % (code, NAMES.get(code, "?"), len(payload)))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7355)
    ap.add_argument("--user", default="Moritz", help="username nel CONNECT")
    ap.add_argument("--call", default="Moritz", help="nominativo nel CONNECT; vuoto -> NoCall #n")
    ap.add_argument("--permissions", type=lambda s: int(s, 0), default=0,
                    help="permessi richiesti nel CONNECT; il server li riscrive comunque a 7")
    ap.add_argument("--fixture", choices=sorted(FIXTURES), default="first_over",
                    help="byte di keying da mandare (default first_over); "
                         "'long' e' i 104 elementi sintetici di cwnet_jitter.py")
    ap.add_argument("--file", default=None,
                    help="frame CWNet grezzi da un file, al posto della fixture")
    ap.add_argument("--delay", type=float, default=0.2,
                    help="secondi fra l'eco del CONNECT e l'invio (default 0.2)")
    ap.add_argument("--hold", type=float, default=3.0,
                    help="secondi di permanenza prima di chiudere (default 3)")
    ap.add_argument("--verbose", action="store_true", help="mostra PING e decodifica il keying")
    cfg = ap.parse_args()

    if cfg.file:
        with open(cfg.file, "rb") as fh:
            payload = fh.read()
    else:
        payload = FIXTURES[cfg.fixture]

    try:
        return Sender(cfg, payload).run()
    except KeyboardInterrupt:
        print("\nfine.")
        return 0
    except OSError as e:
        print("errore: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
