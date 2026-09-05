---
module: keyer_cwnet
date: 2026-09-05
problem_type: architecture_pattern
component: testing_framework
severity: high
applies_when:
  - "Serve dimostrare la conformità di keyer_cwnet al Remote CW Keyer di DL4YHF"
  - "Un comportamento del protocollo poggia su una lettura di sorgente non ancora osservata sul filo"
  - "Il riferimento è un binario Windows/Borland non ricompilabile su questa toolchain"
  - "Il traffico da catturare gira su loopback e Npcap/Wireshark non lo intercetta"
root_cause: missing_tooling
resolution_type: tooling_addition
related_components:
  - test_host
tags:
  - cwnet
  - dl4yhf
  - oracle
  - conformance
  - differential-testing
  - protocol
  - tcp-tap
---

# Il sorgente del riferimento compilato come oracolo differenziale

## Contesto

La suite host aveva 189 test verdi che asserivano byte di protocollo **mai confrontati con il riferimento**: l'atteso e l'implementazione pescavano dalla stessa `#define`, quindi il test misurava l'implementazione contro sé stessa. È l'episodio `0x15` — il nostro TX manda il keying su `CWNET_CMD_CW_UP = 0x14` e `CWNET_CMD_CW_DOWN = 0x15` (`components/keyer_cwnet/include/cwnet_client.h:63-64`), che nel protocollo vero sono CI-V e spettro; il keying viaggia su `MORSE 0x10`. Verde per costruzione, sbagliato di fatto.

La difficoltà: il golden standard è un eseguibile Windows compilato con Borland C++ Builder 6, non ricompilabile qui, e i suoi sorgenti pubblicati (`Remote_CW_Keyer_Sources.zip`, qsl.net/dl4yhf) sono incompleti — `CwNet.c` include header della libreria personale dell'autore che non sono nell'archivio (`StringLib.h`, `yhf_type.h`, `QFile.h`, `YHF_*.h`) e l'autore non li fornisce.

## Guida

Tre mosse, in ordine di forza probatoria.

**1. Compila il modulo puro del riferimento come oracolo differenziale.** `CwStreamEnc.c` (il codec dello stream di keying) è 230 righe di C puro: dipende solo da `<string.h>` e da quattro typedef (`BYTE`, `WORD`, `DWORD`, `BOOL`), forniti con uno shim `yhf_type.h`. Compila al primo colpo con `clang -I shim`. Non usa `long`, `float` né `double` — solo interi a larghezza fissa — quindi le due fonti classiche di divergenza fra compilatori (modello dati ILP32 vs LP64, x87 vs SSE) **non si applicano**. Il dominio di ingresso è minuscolo (0–1040 ms in un verso, 0–127 nell'altro): il confronto contro il nostro `cwstream_encode_timestamp()` e `cwstream_decode_timestamp()` (`components/keyer_cwnet/src/cwnet_timestamp.c`) si fa **esaustivo, non campionato**. Esito: zero scarti su tutto il dominio, con UBSan pulito.

**2. Quando il suo strato di rete non compila, non ti serve.** `CwNet.c` (4177 righe, 11 include di progetto) è fuori portata, ma il suo strato di rete non aggiunge verità: i byte sul filo li legge il *nostro* parser (`cwnet_frame_parse()`, `components/keyer_cwnet/include/cwnet_frame.h:124`). Alimentando i flussi grezzi al nostro parser più il *suo* codec per il payload, si decodifica una cattura reale senza ricompilare una riga della sua rete.

**3. Cattura il loopback con un tap TCP, non con Npcap.** Quando client e server DL4YHF girano nella stessa VM e il loopback non è catturabile, un relay TCP trasparente in Python (il client punta al tap, il tap inoltra al server) registra i due versi come byte grezzi. Niente Wireshark, niente Npcap, niente sudo, niente estrazione pcap. L'unico effetto collaterale è la ri-segmentazione TCP, irrilevante perché a valle si lavora sul flusso riassemblato.

## Perché conta

Un oracolo ricompilato da un sorgente pubblicato è un riferimento **più debole** del binario in distribuzione: possono divergere (il CI-V del sorgente è dimostrabilmente più avanti dell'app). Quindi l'oracolo non è il riferimento — è un candidato che si guadagna il posto. Il suo primo test è: *dato lo stesso stimolo, riproduce byte per byte una cattura del binario vero?* Se sì, ha dimostrato la sua fedeltà su quel percorso e può generare i casi che la cattura non copre. Se no, non hai perso nulla: hai scoperto **dove** sorgente e binario divergono, che è l'informazione che mancava.

Questo inverte il difetto dell'episodio `0x15`: l'atteso smette di venire da noi e viene da un artefatto esterno falsificabile.

## Quando applicarla

Prima di asserire come si comporta il protocollo DL4YHF, e ogni volta che una decisione poggia su una lettura del suo sorgente. Il sorgente dice **cosa guardare**, non cosa è vero: la conferma viene dai byte. Corollario emerso qui — il commento di `CwStreamEnc.c` sul fine over dice «ten dot-times or 500 ms», ma il codice (`KeyerThread.c`, `t_us > 14000 * iDotTime_ms`) e la cattura dicono **14 dot-time**. Il commento è sorgente quanto il codice, e può mentire; arbitrano i byte.

## Esempi

Confronto esaustivo del codec (l'oracolo differenziale):

```c
/* diff_main.c — suo codec vs nostro, tutto il dominio */
for (int ms = -50; ms <= 2000; ms++)
    if (CwStreamEnc_MillisecondsTo7BitTimestamp(ms) != cwstream_encode_timestamp(ms)) eb++;
for (int b = 0; b <= 127; b++)
    if (CwStreamEnc_7BitTimestampToMilliseconds((BYTE)b) != cwstream_decode_timestamp((uint8_t)b)) db++;
/* -> ENCODE 0/2051, DECODE 0/128, UBSan pulito */
```

```bash
clang -O1 -fsanitize=undefined -I shim -I <ref-src> -I <our-include> \
      diff_main.c <ref-src>/CwStreamEnc.c <our-src>/cwnet_timestamp.c -o difftest
```

Il tap che rende catturabile un loopback non catturabile:

```
client CWNet (VM) --> tap TCP (Mac, 192.168.179.1:7355) --> server CWNet (VM)
                          |                                   scrive i due versi
                          '-> sess_N_client_to_server.bin      come byte grezzi
                              sess_N_server_to_client.bin
```

Gli strumenti (`shim/yhf_type.h`, `difftest`, `cwnet_dump`, `pcap_to_stream.py`, `cwnet_tap.py`) sono l'unità U2 del piano banco-prova-cwnet, sbloccata, e vanno in `tools/cwnet/`. Le catture prodotte **non** si committano finché la issue #8 (provenienza) è aperta.
