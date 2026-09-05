#!/usr/bin/env python3
"""Estrae i flussi TCP da un pcap/pcapng e li scrive come byte grezzi, una
direzione per file. Solo libreria standard: niente tshark, niente dipendenze in CI.

  python3 pcap_to_stream.py cattura.pcapng --port 7355 --out sessione1
"""
import argparse, struct, sys
from collections import defaultdict

LT_ETHERNET, LT_NULL, LT_RAW, LT_SLL = 1, 0, 101, 113


def read_pcap(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:4]
    if magic in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        endian = ">"
    elif magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
        endian = "<"
    elif magic == b"\x0a\x0d\x0d\x0a":
        return read_pcapng(data)
    else:
        sys.exit(f"formato non riconosciuto: magic {magic.hex()}")
    linktype = struct.unpack(endian + "I", data[20:24])[0]
    off, pkts = 24, []
    while off + 16 <= len(data):
        _, _, incl, _ = struct.unpack(endian + "IIII", data[off:off + 16])
        off += 16
        pkts.append(data[off:off + incl])
        off += incl
    return linktype, pkts


def read_pcapng(data):
    endian, off, linktype, pkts = "<", 0, None, []
    while off + 12 <= len(data):
        btype, = struct.unpack(endian + "I", data[off:off + 4])
        if btype == 0x0A0D0D0A:
            bom, = struct.unpack("<I", data[off + 8:off + 12])
            endian = "<" if bom == 0x1A2B3C4D else ">"
        blen, = struct.unpack(endian + "I", data[off + 4:off + 8])
        if blen < 12:
            break
        body = data[off + 8:off + blen - 4]
        if btype == 0x00000001:
            linktype = struct.unpack(endian + "H", body[0:2])[0]
        elif btype == 0x00000006:
            caplen = struct.unpack(endian + "I", body[12:16])[0]
            pkts.append(body[20:20 + caplen])
        off += blen
    return (linktype if linktype is not None else LT_ETHERNET), pkts


def strip_link(lt, pkt):
    if lt == LT_ETHERNET:
        if len(pkt) < 14:
            return None
        etype = struct.unpack("!H", pkt[12:14])[0]
        off = 14
        while etype in (0x8100, 0x88A8):
            etype = struct.unpack("!H", pkt[off + 2:off + 4])[0]
            off += 4
        return pkt[off:] if etype == 0x0800 else None
    if lt == LT_NULL:
        return pkt[4:] if len(pkt) >= 4 else None
    if lt == LT_SLL:
        return pkt[16:] if len(pkt) >= 16 else None
    if lt == LT_RAW:
        return pkt
    return None


def parse_tcp(ip):
    if len(ip) < 20 or (ip[0] >> 4) != 4 or ip[9] != 6:
        return None
    ihl = (ip[0] & 0x0F) * 4
    total = struct.unpack("!H", ip[2:4])[0]
    src = ".".join(str(b) for b in ip[12:16])
    dst = ".".join(str(b) for b in ip[16:20])
    tcp = ip[ihl:total if total else len(ip)]
    if len(tcp) < 20:
        return None
    sport, dport, seq = struct.unpack("!HHI", tcp[0:8])
    doff = (tcp[12] >> 4) * 4
    return (src, sport, dst, dport, seq, tcp[doff:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("--port", type=int, help="tieni solo i flussi che toccano questa porta")
    ap.add_argument("--out", default="stream")
    a = ap.parse_args()

    lt, pkts = read_pcap(a.pcap)
    segs = defaultdict(dict)
    for pkt in pkts:
        ip = strip_link(lt, pkt)
        if not ip:
            continue
        t = parse_tcp(ip)
        if not t:
            continue
        src, sp, dst, dp, seq, payload = t
        if a.port and a.port not in (sp, dp):
            continue
        if payload:
            segs[(src, sp, dst, dp)].setdefault(seq, payload)

    if not segs:
        sys.exit("nessun payload TCP trovato (porta sbagliata? cattura vuota?)")

    print(f"linktype {lt}, {len(pkts)} pacchetti, {len(segs)} direzioni con dati\n")
    for i, (key, bysec) in enumerate(sorted(segs.items()), 1):
        src, sp, dst, dp = key
        out, expect, gaps = bytearray(), None, 0
        for seq in sorted(bysec):
            p = bysec[seq]
            if expect is not None and seq != expect:
                gaps += 1
            out += p
            expect = seq + len(p)
        name = f"{a.out}_{i}_{src}-{sp}_to_{dst}-{dp}.bin"
        with open(name, "wb") as f:
            f.write(out)
        flag = f"   ATTENZIONE: {gaps} discontinuita' di sequenza" if gaps else ""
        print(f"  {name}  {len(out)} byte{flag}")


if __name__ == "__main__":
    main()
