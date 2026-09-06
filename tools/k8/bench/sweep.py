#!/usr/bin/env python3
"""Banco differenziale: stesso stimolo al K8 emulato e alla nostra FSM, diff."""
import subprocess, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),'k8cmp'))
import k8seq
U = 48000                                   # unita a 25 WPM, us
OURS = os.path.join(os.path.dirname(os.path.abspath(__file__)),'ours_seq')

def ours(ev, mode_b, tend):
    txt = ''.join(f'{t} {d} {a}\n' for (t,d,a) in ev)
    r = subprocess.run([OURS,'25','1' if mode_b else '0',str(tend)],
                       input=txt, capture_output=True, text=True)
    return r.stdout.strip()

def cases():
    q = lambda x: int(round(x*U))
    # A: dah primo, dit dopo, rilasci sfalsati
    for a in (0.25,0.5,1.0,1.5):
        for b in (1.0,2.0,3.0,4.0,5.0):
            for d in (-0.5,0.25,0.5,1.0,2.0):
                c = b+d
                if c <= a or b <= a: continue
                yield ('A',a,b,c), [(0,0,1),(q(a),1,1),(q(b),1,0),(q(c),0,0)]
    # B: speculare, dit primo
    for a in (0.25,0.5,1.0,1.5):
        for b in (1.0,2.0,3.0,4.0,5.0):
            for d in (-0.5,0.25,0.5,1.0,2.0):
                c = b+d
                if c <= a or b <= a: continue
                yield ('B',a,b,c), [(0,1,0),(q(a),1,1),(q(b),0,1),(q(c),0,0)]
    # C: una leva tenuta, tap dell'opposta a fasi diverse
    for hold_dah in (True,False):
        for ph in (0.3,0.8,1.3,1.8,2.3,2.8,3.3):
            for w in (0.2,0.5,1.0):
                ev=[(0,0,1)] if hold_dah else [(0,1,0)]
                if hold_dah: ev += [(q(ph),1,1),(q(ph+w),0,1),(q(6.0),0,0)]
                else:        ev += [(q(ph),1,1),(q(ph+w),1,0),(q(6.0),0,0)]
                yield ('C','dah' if hold_dah else 'dit',ph,w), ev

def main():
    diffs=[]; n=0
    for mode_b in (False,True):
        for key,ev in cases():
            tend = ev[-1][0] + 8*U
            k = k8seq.run(ev, tail_us=8*U, tag='s')
            o = ours(ev, mode_b, tend)
            n+=1
            if k!=o: diffs.append((mode_b,key,k,o))
    print(f'casi confrontati: {n}   divergenze: {len(diffs)}')
    for mb,key,k,o in diffs[:40]:
        print(f'  mode {"B" if mb else "A"} {key}   K8={k!r}  noi={o!r}')
if __name__=='__main__': main()
