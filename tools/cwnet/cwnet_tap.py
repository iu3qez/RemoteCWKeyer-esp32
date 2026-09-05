#!/usr/bin/env python3
"""Relay TCP trasparente che registra i byte nei due versi.

Il client CWNet si connette a questo processo invece che al server; il processo
inoltra al server vero e scrive due file di byte grezzi, uno per direzione,
nello stesso formato che cwnet_dump si aspetta.

Non interpreta il protocollo: copia byte. L'unica cosa che cambia rispetto a una
connessione diretta e' la segmentazione TCP, che e' irrilevante perche' a valle
si lavora sul flusso riassemblato.

  python3 cwnet_tap.py --listen 0.0.0.0:7355 --server 192.168.179.128:7355 --out sess1
"""
import argparse, socket, threading, sys, time

def hostport(s):
    h, _, p = s.rpartition(":")
    return h, int(p)

def pump(src, dst, f, label, stop):
    n = 0
    try:
        while not stop.is_set():
            b = src.recv(65536)
            if not b:
                break
            f.write(b); f.flush()
            n += len(b)
            print(f"  {label}: +{len(b):5d} byte (totale {n})", flush=True)
            dst.sendall(b)
    except OSError:
        pass
    finally:
        stop.set()
        for s in (src, dst):
            try: s.shutdown(socket.SHUT_RDWR)
            except OSError: pass
    print(f"  {label}: chiuso, {n} byte totali", flush=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", default="0.0.0.0:7355")
    ap.add_argument("--server", required=True, help="IP:porta del server CWNet vero")
    ap.add_argument("--out", default="sess", help="prefisso dei file di uscita")
    a = ap.parse_args()

    lh, lp = hostport(a.listen)
    sh, sp = hostport(a.server)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((lh, lp)); srv.listen(4)
    print(f"in ascolto su {lh}:{lp}, inoltro a {sh}:{sp}")
    print("il client CWNet deve puntare a questo indirizzo. Ctrl-C per finire.\n")

    conn_no = 0
    try:
        while True:
            cli, addr = srv.accept()
            conn_no += 1
            print(f"[{time.strftime('%H:%M:%S')}] connessione {conn_no} da {addr[0]}:{addr[1]}")
            try:
                up = socket.create_connection((sh, sp), timeout=10)
            except OSError as e:
                print(f"  impossibile raggiungere il server: {e}")
                cli.close(); continue
            cli.settimeout(None); up.settimeout(None)
            f_c2s = open(f"{a.out}_{conn_no}_client_to_server.bin", "wb")
            f_s2c = open(f"{a.out}_{conn_no}_server_to_client.bin", "wb")
            stop = threading.Event()
            threading.Thread(target=pump, args=(cli, up, f_c2s, f"c{conn_no} client->server", stop), daemon=True).start()
            threading.Thread(target=pump, args=(up, cli, f_s2c, f"c{conn_no} server->client", stop), daemon=True).start()
    except KeyboardInterrupt:
        print("\nfine.")

if __name__ == "__main__":
    main()
