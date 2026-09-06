---
title: "Il conteggio di un oracolo differenziale non è un gradiente"
date: 2026-09-06
category: architecture-patterns
module: keyer_iambic
problem_type: workflow_issue
component: testing_framework
severity: high
applies_when:
  - "Un oracolo differenziale restituisce un conteggio di divergenze invece di un verdetto binario"
  - "Si sta per modificare una macchina a stati e rimisurare per decidere se tenere la modifica"
  - "Lo sweep gira su una famiglia di stimoli campionata, non sul dominio esaustivo"
tags:
  - differential-testing
  - oracle
  - method
  - k8
  - keyer-iambic
  - debugging
---

# Il conteggio di un oracolo differenziale non è un gradiente

## Contesto

Il banco differenziale del keyer (`tools/k8/bench/`) manda lo stesso stimolo di
paddle al K1EL K8 emulato in gpsim e alla nostra FSM, poi confronta le sequenze
di elementi. Su 436 casi a 25 WPM partiva da **124 divergenze**, con una
diagnosi corretta della causa (issue #44, oggi iu3qez/Esp32KeyerTest#1) e una raccomandazione motivata su come
correggerla.

L'implementazione è stata misurata dopo ogni passo:

| passo | divergenze |
|---|---|
| stato di partenza | 124 |
| dopo il riordino delle decisioni | 225 |
| dopo la correzione su `BOTH_ON` | 145 |

Tre tentativi, tutti peggiori del punto di partenza, e il lavoro è stato
abbandonato su un ramo `wip-` senza mai capire quale riga fosse sbagliata.

Il banco era corretto. La diagnosi era in larga parte corretta. **A essere
sbagliato è stato l'uso del numero.**

## Guida

**1. Il conteggio risponde a "va meglio", mai a "perché".** È un cancello, non
un gradiente. Serve a decidere se una correzione è finita, non a scegliere quale
correzione fare.

**2. Prima di cambiare una riga, traccia UN caso divergente da un capo
all'altro sui due lati.** Il nostro tick per tick, il riferimento istruzione per
istruzione nell'emulatore. Il caso si sceglie dalla classe più numerosa, così la
comprensione che ne esce copre la maggior parte delle divergenze.

**3. Una modifica si giustifica con la traccia, non con il delta.** Se non sai
dire in anticipo in che verso si muoverà il conteggio e all'incirca di quanto,
non hai ancora capito la modifica che stai per fare.

**4. Guarda sempre la composizione, non solo il totale.** Raggruppa le
divergenze per coppia `(riferimento, nostro)` e conta ogni forma. Il totale
nasconde esattamente l'informazione che serve.

**5. Se abbandoni, conserva il tentativo con i suoi numeri e la diagnosi del
metodo**, non solo del codice. Un ramo `wip-` con un messaggio di commit onesto
costa niente e impedisce al tentativo successivo di rifare la stessa strada.

## Perché conta

Il pattern gemello, `reference-source-as-differential-oracle.md`, descrive un
confronto **esaustivo** che restituisce una risposta binaria: zero scarti su
tutto il dominio, oppure no. Quella forma non si presta a questo abuso.

Uno sweep su una famiglia di stimoli campionata restituisce invece un
**conteggio**, e un conteggio somiglia a un gradiente. Non lo è, per tre
ragioni: i casi non sono indipendenti, una singola modifica può sistemare una
classe e romperne due, e il totale non dice quale.

Il caso concreto è istruttivo. Passare da 124 a 145 sembrava "quasi tornato al
punto di partenza". Ma la composizione era cambiata del tutto: 141 delle 145
erano ormai una forma sola, "perdiamo l'elemento finale", che non era il difetto
originale. Il numero diceva "un po' peggio". La composizione diceva "bug
diverso".

## Quando applicarla

Ogni volta che l'oracolo restituisce un conteggio invece di un verdetto. E in
particolare nel momento in cui ti accorgi di star lanciando lo sweep per
decidere se tenere una modifica: quello è il segnale che stai usando il numero
come gradiente.

## Esempi

Il raggruppamento che avrebbe intercettato l'errore al primo giro, invece che al
terzo:

```python
import collections
c = collections.Counter()
for key, ev in cases():
    k = k8seq.run(ev)          # riferimento
    o = ours(ev)               # nostro
    if k != o:
        c[(k, o)] += 1
for (k, o), n in c.most_common(8):
    print(f"{n:4d}x  rif={k!r:10s} noi={o!r:10s}")
```

Uscita al terzo tentativo, che rende evidente il cambio di natura del difetto:

```
  65x  rif='.-.'      noi='.-'
  32x  rif='..-'      noi='.-'
  20x  rif='....'     noi='...'
  17x  rif='-.-'      noi='-.'
```

Quattro forme, tutte "manca l'ultimo elemento". Un totale di 145 non lo dice.

## Related

- `docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md`
  — il pattern di cui questo documenta un modo di fallire
- `components/keyer_iambic/tools/k8/bench/` — lo scheletro del banco, nel submodule
  Esp32KeyerTest; il suo README dice perché lo sweep sintetico è superato
- `components/keyer_iambic/tools/k8/README.md` — come far girare l'oracolo K8 in gpsim
- iu3qez/Esp32KeyerTest#1 (il difetto, trasferita da #44), #32 (la specifica verificata del riferimento)
- Ramo `wip-k8-decision-order-attempt` — il tentativo abbandonato, da non mergiare
