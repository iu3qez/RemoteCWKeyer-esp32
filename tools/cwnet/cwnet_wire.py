"""
Il formato del filo CWNet, in un posto solo.

Gli strumenti di questa directory parlano tutti lo stesso protocollo: l'eco
(`cwnet_echo.py`) da un capo, il client di prova (`cwnet_send.py`) dall'altro,
la misura (`cwnet_jitter.py`) sopra il secondo. Composizione dei frame,
lunghezze little-endian e codec delle attese a 7 bit stanno qui perche' due
copie della stessa idea del filo divergono, e quando divergono lo strumento
mente sul protocollo invece di provarlo.

La verita' resta `components/keyer_cwnet/` e la cattura del 2026-09-05: questo
modulo la rispecchia, non la decide.
"""

# Codici comando, bit 5-0 del byte di comando (CwNet.h)
CMD_CONNECT, CMD_DISCONNECT, CMD_PING, CMD_PRINT, CMD_TX_INFO, CMD_RIG, CMD_MORSE = (
    1, 2, 3, 4, 5, 6, 0x10)

NAMES = {1: "CONNECT", 2: "DISCONNECT", 3: "PING", 4: "PRINT", 5: "TX_INFO",
         6: "RIG", 0x10: "MORSE", 0x14: "CI_V", 0x15: "SPECTRUM", 0x16: "FREQ_REPORT"}


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


def encode7(ms):
    """Inversa di decode7, stessa divisione intera troncata di
    cwstream_encode_timestamp() (components/keyer_cwnet/src/cwnet_timestamp.c)."""
    if ms < 0:
        return 0
    if ms <= 31:
        return ms
    if ms <= 156:
        return 0x20 + (ms - 32) // 4
    if ms <= 1165:
        return 0x40 + (ms - 157) // 16
    return 0x7F


class FrameParser:
    """Parser a flusso: restituisce (comando, payload) a frame completo."""

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
                raise ValueError("categoria riservata nel comando 0x%02X" % cmd)
            hdr = 2 if cat == 1 else 3
            if len(self.buf) < hdr:
                break
            n = self.buf[1] if cat == 1 else self.buf[1] | (self.buf[2] << 8)
            if len(self.buf) < hdr + n:
                break
            out.append((code, bytes(self.buf[hdr:hdr + n])))
            del self.buf[:hdr + n]
        return out
