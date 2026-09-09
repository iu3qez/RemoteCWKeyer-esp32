#!/usr/bin/env python3
"""Server CWNet minimo che rimanda al mittente il keying che riceve.

Il server DL4YHF non rimanda mai MORSE ai client (H6, smentita il 2026-09-05):
il loop di determinismo del banco, R7, ha bisogno di un capo RX nostro. Quello
che la scatola manda deve tornarle identico, e il confronto e' sul suo percorso
di decodifica. Questo processo fa solo cio' che serve a un client per
connettersi e trasmettere, con i byte del server vero dove il client li guarda:

  * eco del CONNECT con i permessi scelti, PRINT di benvenuto, TX_INFO
  * ciclo PING come richiedente, ogni 2 s come il riferimento, e RESPONSE_2
  * "RPRT 0" a ogni stringa di controllo radio (blocco 0x06)
  * TX_INFO quando un client prende la chiave con il primo byte MORSE, e
    "-- nobody --" dopo un secondo di silenzio, come il timer del server vero
  * eco byte per byte di ogni frame MORSE al client che l'ha mandato

Con --record scrive i due versi come byte grezzi, nello stesso formato del tap,
cosi' cwnet_dump li decodifica. Con --delay-ms ritarda ogni risposta, per
simulare una latenza.

  python3 cwnet_echo.py --listen 0.0.0.0:7355 --permissions 7 --record sess
"""
import argparse, asyncio, sys, time

CMD_CONNECT, CMD_DISCONNECT, CMD_PING, CMD_PRINT, CMD_TX_INFO, CMD_RIG, CMD_MORSE = 1, 2, 3, 4, 5, 6, 0x10
NAMES = {1: "CONNECT", 2: "DISCONNECT", 3: "PING", 4: "PRINT", 5: "TX_INFO", 6: "RIG", 0x10: "MORSE",
         0x14: "CI_V", 0x15: "SPECTRUM", 0x16: "FREQ_REPORT"}
NOBODY = b"-- nobody --\0"


def hostport(s):
    h, _, p = s.rpartition(":")
    return h, int(p)


def now_ms():
    return int(time.monotonic() * 1000) & 0x7FFFFFFF


def le32(v):
    return (v & 0xFFFFFFFF).to_bytes(4, "little")


def frame(code, payload=b""):
    """Un frame CWNet: comando con la categoria nei bit 7-6, lunghezza, payload."""
    if not payload:
        return bytes([code & 0x3F])
    if len(payload) <= 255:
        return bytes([0x40 | (code & 0x3F), len(payload)]) + payload
    return bytes([0x80 | (code & 0x3F), len(payload) & 0xFF, len(payload) >> 8]) + payload


def decode7(b):
    """Attesa in ms dai 7 bit bassi, come CwStreamEnc_7BitTimestampToMilliseconds."""
    v = b & 0x7F
    if v <= 0x1F:
        return v
    if v <= 0x3F:
        return 32 + 4 * (v - 0x20)
    return 157 + 16 * (v - 0x40)


class FrameParser:
    """Parser a flusso: restituisce (comando, payload) man mano che i frame sono completi."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data):
        self.buf += data
        out = []
        while self.buf:
            cmd = self.buf[0]
            cat, code = cmd >> 6, cmd & 0x3F
            if cat == 0:
                out.append((code, b""))
                del self.buf[:1]
                continue
            if cat == 3:
                raise ValueError(f"categoria riservata nel comando 0x{cmd:02X}")
            hdr = 2 if cat == 1 else 3
            if len(self.buf) < hdr:
                break
            n = self.buf[1] if cat == 1 else self.buf[1] | (self.buf[2] << 8)
            if len(self.buf) < hdr + n:
                break
            out.append((code, bytes(self.buf[hdr:hdr + n])))
            del self.buf[:hdr + n]
        return out


class Hub:
    """Stato condiviso fra i client: chi ha la chiave, e l'annuncio a tutti."""

    def __init__(self, key_hold_ms):
        self.clients = {}
        self.next_index = 1
        self.holder = None
        self.last_key_ms = 0
        self.key_hold_ms = key_hold_ms

    def tx_info(self):
        if self.holder is None or self.holder not in self.clients:
            return frame(CMD_TX_INFO, b"\xff" + NOBODY)
        c = self.clients[self.holder]
        return frame(CMD_TX_INFO, bytes([self.holder]) + c.callsign.encode() + b"\0")

    async def announce(self):
        f = self.tx_info()
        for c in list(self.clients.values()):
            await c.send(f, note="TX_INFO")

    async def take_key(self, index):
        """Il server vero da' la chiave al primo byte MORSE se nessuno la tiene."""
        self.last_key_ms = now_ms()
        if self.holder is None:
            self.holder = index
            await self.announce()
        return self.holder == index

    async def release_loop(self):
        while True:
            await asyncio.sleep(0.1)
            if self.holder is not None and now_ms() - self.last_key_ms > self.key_hold_ms:
                self.holder = None
                await self.announce()


class Client:
    def __init__(self, hub, index, reader, writer, cfg):
        self.hub, self.index, self.reader, self.writer, self.cfg = hub, index, reader, writer, cfg
        self.username = "?"
        self.callsign = "?"
        self.parser = FrameParser()
        self.ping_task = None
        self.ping_id = 0
        self.ping_t0 = {}
        self.rec_c2s = self.rec_s2c = None
        if cfg.record:
            self.rec_c2s = open(f"{cfg.record}_{index}_client_to_server.bin", "wb")
            self.rec_s2c = open(f"{cfg.record}_{index}_server_to_client.bin", "wb")

    def log(self, msg):
        print(f"[{time.strftime('%H:%M:%S')}] c{self.index} {msg}", flush=True)

    async def send(self, data, note=""):
        if self.cfg.delay_ms:
            await asyncio.sleep(self.cfg.delay_ms / 1000)
        if self.rec_s2c:
            self.rec_s2c.write(data)
            self.rec_s2c.flush()
        self.writer.write(data)
        await self.writer.drain()
        if note and self.cfg.verbose:
            self.log(f"<- {note}")

    async def ping_loop(self):
        while True:
            self.ping_id = self.ping_id % 255 + 1
            t0 = now_ms()
            self.ping_t0[self.ping_id] = t0
            await self.send(frame(CMD_PING, bytes([0, self.ping_id, 0, 0]) + le32(t0) + bytes(8)),
                            note=f"PING request id={self.ping_id}")
            await asyncio.sleep(self.cfg.ping_interval)

    async def on_frame(self, code, payload):
        if code == CMD_CONNECT and len(payload) == 92:
            self.username = payload[:44].split(b"\0", 1)[0].decode("ascii", "replace")
            call = payload[44:88].split(b"\0", 1)[0].decode("ascii", "replace")
            self.callsign = call if call else f"NoCall #{self.index}"
            self.log(f"CONNECT user={self.username!r} call={self.callsign!r}, permessi 0x{self.cfg.permissions:02X}")
            await self.send(frame(CMD_CONNECT, payload[:88] + le32(self.cfg.permissions)), note="CONNECT echo")
            greeting = f"Welcome {self.username}. This is the echo server: what you key comes back.".encode() + b"\0"
            await self.send(frame(CMD_PRINT, greeting), note="PRINT")
            await self.send(self.hub.tx_info(), note="TX_INFO")
            if self.ping_task is None:
                self.ping_task = asyncio.create_task(self.ping_loop())
        elif code == CMD_PING and len(payload) == 16:
            kind, pid = payload[0], payload[1]
            if kind == 1:
                t0 = self.ping_t0.pop(pid, None)
                t2 = now_ms()
                await self.send(frame(CMD_PING, bytes([2, pid, 0, 0]) + payload[4:8] + payload[8:12] + le32(t2)),
                                note=f"PING response 2 id={pid}")
                if t0 is not None:
                    self.log(f"PING id={pid}: RTT {t2 - t0} ms")
            else:
                self.log(f"PING tipo {kind} inatteso da un client")
        elif code == CMD_MORSE:
            mine = await self.hub.take_key(self.index)
            if self.cfg.verbose:
                ev = " ".join(f"{'D' if b & 0x80 else 'u'}{decode7(b)}" for b in payload)
                self.log(f"-> MORSE {len(payload)} byte: {ev}" + ("" if mine else "  (chiave altrui: il server vero lo scarterebbe)"))
            await self.send(frame(CMD_MORSE, payload), note=f"MORSE echo {len(payload)} byte")
        elif code == CMD_RIG:
            text = payload.split(b"\0", 1)[0].decode("ascii", "replace").strip()
            self.log(f"-> 0x06 {text!r}")
            await self.send(frame(CMD_RIG, b"RPRT 0\n\0"), note="RPRT 0")
        elif code == CMD_DISCONNECT:
            self.log("DISCONNECT")
            self.writer.close()
        else:
            self.log(f"-> 0x{code:02X} {NAMES.get(code, '?')} {len(payload)} byte, ignorato")

    async def run(self):
        peer = self.writer.get_extra_info("peername")
        self.log(f"connesso da {peer[0]}:{peer[1]}")
        self.hub.clients[self.index] = self
        try:
            while True:
                data = await self.reader.read(4096)
                if not data:
                    break
                if self.rec_c2s:
                    self.rec_c2s.write(data)
                    self.rec_c2s.flush()
                for code, payload in self.parser.feed(data):
                    await self.on_frame(code, payload)
        except (ConnectionError, ValueError, asyncio.IncompleteReadError) as e:
            self.log(f"errore: {e}")
        finally:
            if self.ping_task:
                self.ping_task.cancel()
            self.hub.clients.pop(self.index, None)
            if self.hub.holder == self.index:
                self.hub.holder = None
                await self.hub.announce()
            for f in (self.rec_c2s, self.rec_s2c):
                if f:
                    f.close()
            self.writer.close()
            self.log("chiuso")


async def serve(cfg):
    hub = Hub(cfg.key_hold_ms)

    async def on_connect(reader, writer):
        c = Client(hub, hub.next_index, reader, writer, cfg)
        hub.next_index += 1
        await c.run()

    lh, lp = hostport(cfg.listen)
    server = await asyncio.start_server(on_connect, lh, lp)
    print(f"echo server in ascolto su {lh}:{lp}, permessi 0x{cfg.permissions:02X}, "
          f"ping ogni {cfg.ping_interval:g} s" + (f", ritardo {cfg.delay_ms} ms" if cfg.delay_ms else ""))
    print("il client CWNet deve puntare a questo indirizzo. Ctrl-C per finire.\n", flush=True)
    asyncio.create_task(hub.release_loop())
    async with server:
        await server.serve_forever()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--listen", default="0.0.0.0:7355")
    ap.add_argument("--permissions", type=lambda s: int(s, 0), default=7,
                    help="bitmask nel CONNECT echo: TALK 1, TRANSMIT 2, CTRL_RIG 4, ADMIN 8 (default 7)")
    ap.add_argument("--ping-interval", type=float, default=2.0, help="secondi fra i PING (il riferimento: 2)")
    ap.add_argument("--key-hold-ms", type=int, default=1000, help="silenzio dopo cui la chiave torna a nessuno (il riferimento: 1000)")
    ap.add_argument("--delay-ms", type=int, default=0, help="ritardo di ogni risposta, per simulare latenza")
    ap.add_argument("--record", default=None, help="prefisso dei file di byte grezzi, uno per verso e per connessione")
    ap.add_argument("--verbose", action="store_true", help="logga ogni frame e decodifica il keying")
    cfg = ap.parse_args()
    try:
        asyncio.run(serve(cfg))
    except KeyboardInterrupt:
        print("\nfine.")


if __name__ == "__main__":
    main()
