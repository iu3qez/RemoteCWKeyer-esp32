#!/usr/bin/env python3
"""Passa uno stimolo di paddle al K8 emulato e restituisce la sequenza di elementi.
Stimolo: lista di (t_us, dit, dah), tempi nel nostro dominio a 25 WPM."""
import subprocess, sys, re, os, tempfile
SCALE   = 96000.0/96588.0     # us per ciclo, allineamento elemento dit @ TIMEBASE 40
BASE    = 1_500_000           # cicli: dopo il sign-on, la build senza SLEEP non dorme
DIT_MARK= 48326               # cicli, misurati
HEX     = os.path.join(os.path.dirname(os.path.abspath(__file__)),'morse8_tb40_nosleep.hex')

def cyc(us): return BASE + int(round(us/SCALE))

def run(events, tail_us=800_000, tag='q'):
    d = tempfile.mkdtemp(prefix='k8_')
    log = os.path.join(d, f'{tag}.log')
    def stim(name, sel):
        out=[f'stimulus asynchronous_stimulus','initial_state 1','start_cycle 0','{']
        pairs=[]
        for (t,di,da) in events:
            lvl = 0 if sel(di,da) else 1      # premuto = 0
            pairs.append(f'{cyc(t)}, {lvl}')
        out.append('  '+',  '.join(pairs))
        out += ['}', f'name {name}', 'end']
        return '\n'.join(out)
    end = cyc(events[-1][0]) + int(round(tail_us/SCALE))
    stc = '\n'.join([
        'frequency 4024500',
        stim('dit_stim', lambda di,da: di),
        stim('dah_stim', lambda di,da: da),
        'stimulus asynchronous_stimulus','initial_state 1','start_cycle 0','{ 1, 1 }','name pb_stim','end',
        'node n_dit n_dah n_pb',
        'attach n_dit dit_stim gpio0',
        'attach n_dah dah_stim gpio1',
        'attach n_pb  pb_stim  gpio3',
        f'log on {log}', 'log w gpio', f'break c {end}', 'run', 'log off', 'quit', ''])
    stcp = os.path.join(d,f'{tag}.stc'); open(stcp,'w').write(stc)
    subprocess.run(['gpsim','-i','-p','pic12c509','-c',stcp,HEX],
                   cwd=d, capture_output=True, timeout=600)
    # estrai i fronti su KEY (bit 2 di gpio)
    cycn=None; edges=[]
    for line in open(log, errors='ignore'):
        m=re.match(r'0x([0-9A-Fa-f]+)\s+p12c509', line)
        if m: cycn=int(m.group(1),16); continue
        m=re.search(r'Wrote: 0x([0-9A-Fa-f]+) to gpio\(0x0006\) was 0x([0-9A-Fa-f]+)', line)
        if m and cycn is not None:
            new,old=int(m.group(1),16),int(m.group(2),16)
            if ((new>>2)&1)!=((old>>2)&1): edges.append((cycn,(new>>2)&1))
    seq=''; start=None
    for c,l in edges:
        if l==1: start=c
        elif start is not None:
            seq += '.' if (c-start) < 2*DIT_MARK else '-'
            start=None
    return seq

if __name__=='__main__':
    ev=[tuple(int(x) for x in l.split()) for l in sys.stdin if l.strip()]
    print(run(ev))
