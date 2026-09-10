#!/usr/bin/env python3
"""Misura lo scarto fra le righe di 'key' sull'uscita virtuale di cwnetd e le
attese codificate nel filo (U7, Success Criteria KTD3).

Metodo
------
key_output.h scrive ogni fronte come "key <0|1> <ms>", dove <ms> e' l'istante
*programmato* per quel fronte -- il B piu' la somma cumulata delle attese a
7 bit decodificate dal MORSE ricevuto (cwnet_play.h). Quel numero e' "le
attese codificate": non lo ricalcoliamo, lo leggiamo dalla riga.

Quello che questo script aggiunge e' *quando* la riga arriva davvero: legge
stdout del daemon con una pipe e, a ogni riga, prende un timestamp sul
proprio orologio monotono (time.monotonic()). Sperimentalmente, su questo
Mac time.monotonic() di Python e clock_gettime(CLOCK_MONOTONIC) di
host/platform/clock.c NON condividono l'epoca fra i due processi (scarto
misurato: circa 1.25e8 ms costante, non zero) -- diversamente da Linux, dove
CLOCK_MONOTONIC e' la stessa epoca per ogni processo. Lo script quindi si
calibra da solo: prende lo scarto (lettura - programmato) del primo fronte
come riferimento e sottrae quel riferimento da tutti gli scarti successivi.
Quello che resta e' la DERIVA rispetto al primo fronte -- ritardo end-to-end
fra "cwnetd ha deciso che il fronte doveva succedere a t" e "un lettore
esterno l'ha visto sull'uscita virtuale", lettura della pipe Python compresa
-- indipendente da un'eventuale offset costante fra le due epoche.

NON e' una misura con l'oscilloscopio sul tasto vero: quella e' il passo di
banco con la scatola (R18, host/cwnetd/README.md), che la CI non fa e che
esegue il maintainer. Questo e' uno scarto misurato su una macchina, non una
garanzia RT.

Uso
---
    python3 cwnet_jitter.py                         # misura di jitter (fixture 'long')
    python3 cwnet_jitter.py --handover               # tempo di scambio (fixture 'first_over')
    python3 cwnet_jitter.py --cwnetd ../../host/build/cwnetd --port 17400
"""
import argparse
import os
import re
import signal
import statistics
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CWNETD = os.path.join(HERE, "..", "..", "host", "build", "cwnetd")
SEND_PY = os.path.join(HERE, "cwnet_send.py")

sys.path.insert(0, HERE)
import cwnet_send as cs  # noqa: E402 -- serve solo a decodificare la fixture 'long'


def long_fixture_events():
    """(stato, delay_ms) di ogni evento della fixture 'long', dagli stessi byte
    che cwnet_send.py spedisce -- non un ricalcolo indipendente."""
    frame_bytes = cs.FIXTURES["long"]
    hdr = 2  # 0x50, len(<=255): frame CWNet cat1
    n = frame_bytes[1]
    payload = frame_bytes[hdr:hdr + n]
    return [(bool(b & 0x80), cs.decode7(b)) for b in payload]

EDGE_RE = re.compile(r"^(key|ptt) ([01]) (-?\d+)$")


class DaemonReader:
    """Legge stdout del daemon in un thread, con un timestamp di lettura per riga."""

    def __init__(self, proc):
        self.proc = proc
        self.lines = []  # (wall_ms, testo_riga)
        self.lock = threading.Lock()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        for raw in self.proc.stdout:
            wall_ms = time.monotonic() * 1000.0
            line = raw.rstrip("\n")
            with self.lock:
                self.lines.append((wall_ms, line))
            if "stato ascolto" in line:
                self.ready.set()

    def snapshot(self):
        with self.lock:
            return list(self.lines)


def start_daemon(cwnetd, host, port, play_floor, ptt_tail, idle_ms=None, timeout=5.0):
    args = [cwnetd, "--listen", host, "--port", str(port),
            "--play-floor", str(play_floor), "--ptt-tail", str(ptt_tail),
            "--output", "virtual"]
    if idle_ms is not None:
        args += ["--idle", str(idle_ms)]
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, bufsize=1)
    reader = DaemonReader(proc)
    if not reader.ready.wait(timeout=timeout):
        stop_daemon(proc, reader)
        raise RuntimeError("cwnetd non ha detto 'stato ascolto' entro %.1f s" % timeout)
    return proc, reader


def stop_daemon(proc, reader):
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)
    reader.thread.join(timeout=2)


def run_client(host, port, fixture, hold, call="Moritz"):
    args = [sys.executable, SEND_PY, "--host", host, "--port", str(port),
            "--fixture", fixture, "--call", call, "--hold", str(hold), "--delay", "0.2"]
    subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    timeout=hold + 10)


def edges(lines, what):
    out = []
    for wall_ms, line in lines:
        m = EDGE_RE.match(line)
        if m and m.group(1) == what:
            out.append((wall_ms, int(m.group(2)), int(m.group(3))))
    return out


def measure_jitter(cwnetd, host, port, play_floor, ptt_tail, verbose):
    events = long_fixture_events()
    span_ms = sum(d for _state, d in events[1:])  # dal primo fronte all'ultimo

    # La fixture 'long' manda i suoi ~104 byte in un solo scrivi-e-basta
    # (nessuna ripaginazione sul filo, cwnet_send.py non ritemporizza): il
    # daemon non vede altro traffico dal client per tutta la riproduzione, e
    # --idle (silenzio del titolare) di default e' 5000 ms -- ben sotto lo
    # sviluppo della fixture. Va alzato per questa misura, altrimenti il
    # titolare perde la chiave a meta' sequenza (fault "titolare muto").
    idle_ms = int(span_ms + 5000)
    hold = (span_ms + play_floor + ptt_tail) / 1000.0 + 3.0

    proc, reader = start_daemon(cwnetd, host, port, play_floor, ptt_tail, idle_ms=idle_ms)
    try:
        run_client(host, port, "long", hold)
        time.sleep(0.3)  # lascia arrivare le ultime righe prima di leggere
    finally:
        lines = reader.snapshot()
        stop_daemon(proc, reader)

    key_edges = edges(lines, "key")
    if not key_edges:
        raise RuntimeError("nessuna riga 'key' ricevuta -- vedi output del daemon")

    # Scarto grezzo (lettura Python - programmato dal daemon): le due epoche
    # monotone non coincidono fra i due processi (vedi docstring del modulo),
    # quindi si calibra sul primo fronte e si guarda solo la deriva da li'.
    raw = [wall_ms - m for wall_ms, _state, m in key_edges]
    baseline = raw[0]
    diffs = [d - baseline for d in raw]
    mean_abs = statistics.mean(abs(d) for d in diffs)
    max_abs = max(abs(d) for d in diffs)

    if verbose:
        for (wall_ms, state, m), d in zip(key_edges, diffs):
            print("  key %d  programmato=%d  letto=%.1f  scarto=%.2f ms" %
                  (state, m, wall_ms, d))

    return {
        "n": len(key_edges),
        "expected_n": len(events),
        "mean_abs_ms": mean_abs,
        "max_abs_ms": max_abs,
    }


def measure_handover(cwnetd, host, port, play_floor, ptt_tail, verbose):
    """Tempo di scambio dopo la TX: B + coda, confermato sul loop con
    ref_first_over (che chiude l'over correttamente, doppio key-up finale)."""
    proc, reader = start_daemon(cwnetd, host, port, play_floor, ptt_tail)
    try:
        hold = (play_floor + ptt_tail) / 1000.0 + 3.0
        run_client(host, port, "first_over", hold)
        time.sleep(0.3)
    finally:
        lines = reader.snapshot()
        stop_daemon(proc, reader)

    key_edges = edges(lines, "key")
    ptt_edges = edges(lines, "ptt")
    key_ups = [e for e in key_edges if e[1] == 0]
    ptt_offs = [e for e in ptt_edges if e[1] == 0]
    if not key_ups or not ptt_offs:
        raise RuntimeError("over incompleto: %d key-up, %d ptt-off (vedi output del daemon)" %
                            (len(key_ups), len(ptt_offs)))

    last_key_up_m = key_ups[-1][2]
    ptt_off_m = ptt_offs[-1][2]
    station_interval_ms = ptt_off_m - last_key_up_m

    if verbose:
        for what, lst in (("key", key_edges), ("ptt", ptt_edges)):
            for wall_ms, state, m in lst:
                print("  %s %d  programmato=%d  letto=%.1f" % (what, state, m, wall_ms))

    return {
        "last_key_up_ms": last_key_up_m,
        "ptt_off_ms": ptt_off_m,
        "station_interval_ms": station_interval_ms,
        "formula_ms": play_floor + ptt_tail,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cwnetd", default=DEFAULT_CWNETD, help="path dell'eseguibile cwnetd")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=17390)
    ap.add_argument("--play-floor", type=int, default=100, help="B, ms (default 100, come cwnetd)")
    ap.add_argument("--ptt-tail", type=int, default=100, help="coda del PTT, ms (default 100, come cwnetd)")
    ap.add_argument("--handover", action="store_true",
                    help="misura il tempo di scambio (B + coda) invece del jitter")
    ap.add_argument("--verbose", action="store_true", help="stampa ogni fronte")
    cfg = ap.parse_args()

    cwnetd = os.path.abspath(cfg.cwnetd)
    if not os.path.isfile(cwnetd):
        print("errore: cwnetd non trovato in %s -- compilalo con "
              "'cmake -S host -B host/build && cmake --build host/build'" % cwnetd, file=sys.stderr)
        return 1

    try:
        if cfg.handover:
            r = measure_handover(cwnetd, cfg.host, cfg.port, cfg.play_floor, cfg.ptt_tail, cfg.verbose)
            print("ultimo key-up (stazione, programmato): %d ms" % r["last_key_up_ms"])
            print("PTT giu' (stazione, programmato):       %d ms" % r["ptt_off_ms"])
            print("intervallo stazione ultimo key-up -> PTT giu': %d ms "
                  "(atteso: coda = %d ms)" % (r["station_interval_ms"], cfg.ptt_tail))
            print("tempo di scambio dopo la TX (client -> PTT giu' in stazione) = "
                  "B + coda = %d + %d = %d ms" %
                  (cfg.play_floor, cfg.ptt_tail, r["formula_ms"]))
        else:
            r = measure_jitter(cwnetd, cfg.host, cfg.port, cfg.play_floor, cfg.ptt_tail,
                               cfg.verbose)
            print("fronti 'key' ricevuti: %d/%d attesi" % (r["n"], r["expected_n"]))
            print("scarto medio:   %.3f ms" % r["mean_abs_ms"])
            print("scarto massimo: %.3f ms" % r["max_abs_ms"])
    except RuntimeError as e:
        print("errore: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
