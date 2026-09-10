# tools/cwnet — strumenti del banco di prova CWNet (U2)

Estrazione e decodifica del traffico CWNet contro il golden standard DL4YHF.
Metodo e razionale: [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md).

Nessuna dipendenza esterna: gli script sono Python 3 stdlib, i programmi C
compilano con clang/gcc. In particolare `pcap_to_stream.py` sostituisce
`tshark`, che così **non** diventa una dipendenza della CI.

## Strumenti nostri

| File | Cosa fa |
|---|---|
| `cwnet_echo.py` | Server CWNet minimo per il loop di determinismo (R7, #14): eco del CONNECT con i permessi, PING come richiedente ogni 2 s, `RPRT 0` alle stringhe 0x06, `TX_INFO` con la chiave presa al primo byte MORSE e rilasciata dopo 1 s, ed eco byte per byte di ogni frame MORSE al mittente. Il `TX_INFO` annuncia il **nominativo** del CONNECT (`NoCall #n` se vuoto), non lo username: sono due campi distinti del CONNECT (92 byte: 44 username, 44 nominativo, 4 permessi) e solo il secondo e' quello che un vero server CWNet mostra agli altri client. Il server DL4YHF non rimanda mai MORSE (H6). Registra i due versi come il tap. |
| `cwnet_tap.py` | Relay TCP trasparente: il client CWNet punta al tap, il tap inoltra al server e registra i due versi come byte grezzi. Cattura il loopback quando Wireshark/Npcap non lo intercetta. |
| `cwnet_send.py` | Client CWNet di prova per il daemon vero, `cwnetd` (host/cwnetd): CONNECT, risposta ai PING, invio dei byte grezzi di una fixture (`--fixture first_over`, ..., `--fixture long` per la misura del jitter) o di un file, stampa di TX_INFO/RPRT/PING in chiaro. E' il "loop senza scatola" di U6/U7 — vedi [host/cwnetd/README.md](../../host/cwnetd/README.md). |
| `cwnet_jitter.py` | Misura lo scarto (jitter) fra le righe `key`/`ptt` dell'uscita virtuale di `cwnetd` e le attese che portano scritte, e il tempo di scambio dopo la TX (B + coda). Guida `cwnetd` e `cwnet_send.py` da solo; metodo e numeri misurati: [host/cwnetd/README.md](../../host/cwnetd/README.md#misura-del-jitter-e-del-tempo-di-scambio). |
| `pcap_to_stream.py` | Estrae i flussi TCP da un pcap/pcapng e li scrive come byte grezzi, una direzione per file (solo stdlib, gestisce Ethernet/loopback/SLL/raw). |
| `cwnet_dump.c` | Decodifica un flusso grezzo: parser di frame **nostro** + codec del keying **di DL4YHF**. Diagnostico, non asserisce. |
| `diff_main.c` | Confronto esaustivo del nostro `cwnet_timestamp.c` contro `CwStreamEnc.c` di DL4YHF, tutto il dominio di ingresso. |
| `gen_synth.c` | Genera uno stream MORSE sintetico con l'encoder DL4YHF, per collaudare il decodificatore. |
| `keyer_sim.c` | Simula il `KeyerThread` di DL4YHF (transizioni, cronometro aggiustato dei ms codificati, fine over a 14 dot-time) sull'encoder DL4YHF. Provenienza degli attesi sintetici di `test_cwnet_client.c`; il caso A riproduce i byte del primo over della sessione 12. |
| `shim/yhf_type.h` | I quattro typedef (`BYTE`/`WORD`/`DWORD`/`BOOL`) che l'archivio pubblicato di DL4YHF non include. Scritto da noi. |
| `shim/Elbug.h` | Stub: `CwStreamEnc.c` include `Elbug.h` ma non usa alcun simbolo di Elbug, solo i typedef. Lo stub evita di dover scaricare `Elbug.{c,h}`. Scritto da noi. |

## Sorgente DL4YHF (non vendorizzato qui)

`cwnet_dump.c`, `diff_main.c` e `gen_synth.c` compilano contro `CwStreamEnc.c`
e `CwStreamEnc.h` di DL4YHF. **Permesso:** l'autore (Wolfgang Buescher,
DL4YHF) ha autorizzato per email l'uso libero dei suoi sorgenti. Il modulo
`CwStreamEnc.*` è C puro e dipende solo dallo shim qui sopra.

Non è committato in questo repo per ora — è una decisione a parte dal
committare i nostri strumenti. Per ottenerlo:

```sh
curl -sO https://www.qsl.net/dl4yhf/Remote_CW_Keyer/Remote_CW_Keyer_Sources.zip
unzip -j Remote_CW_Keyer_Sources.zip \
  'Remote_CW_Keyer/sources/CwStreamEnc.c' \
  'Remote_CW_Keyer/sources/CwStreamEnc.h' -d ref/
# archivio verificato: sha256 d960d6b9…, file datati 2025-10-20
```

## Uso

```sh
REF=ref                     # dove hai messo CwStreamEnc.{c,h}
OUR=../../components/keyer_cwnet

# confronto esaustivo del codec (deve dare 0 scarti)
clang -O1 -fsanitize=undefined -I shim -I "$REF" -I "$OUR/include" \
      diff_main.c "$REF/CwStreamEnc.c" "$OUR/src/cwnet_timestamp.c" -o difftest && ./difftest

# decodifica di una cattura
clang -O1 -Wall -Wextra -fsanitize=address,undefined -I shim -I "$REF" -I "$OUR/include" \
      cwnet_dump.c "$REF/CwStreamEnc.c" "$OUR/src/cwnet_frame.c" -o cwnet_dump

# byte attesi dal KeyerThread di riferimento per una lista di edge
clang -O1 -Wall -fsanitize=undefined -I shim -I "$REF" keyer_sim.c "$REF/CwStreamEnc.c" -o keyer_sim && ./keyer_sim

# loop di determinismo: la scatola punta all'echo server, che le rimanda il suo keying
python3 cwnet_echo.py --listen 0.0.0.0:7355 --permissions 7 --verbose --record echo
./cwnet_dump echo_1_client_to_server.bin; ./cwnet_dump echo_1_server_to_client.bin   # stessi frame MORSE nei due versi

# tap dal vivo: client -> tap -> server, un file per direzione
python3 cwnet_tap.py --listen 0.0.0.0:7355 --server <ip-server>:7355 --out sess
# oppure estrai da un pcap:
python3 pcap_to_stream.py cattura.pcapng --port 7355 --out sess
./cwnet_dump sess_1_*.bin

# loop senza scatola: cwnet_send.py fa da client di prova contro il daemon vero
host/build/cwnetd --listen 127.0.0.1 --port 17355 &
python3 cwnet_send.py --host 127.0.0.1 --port 17355 --fixture first_over --verbose

# misura del jitter e del tempo di scambio (guida cwnetd e cwnet_send.py da solo)
python3 cwnet_jitter.py --cwnetd ../../host/build/cwnetd
python3 cwnet_jitter.py --cwnetd ../../host/build/cwnetd --handover
```

Il daemon vero (`cwnetd`) e il suo README — avvio, flag, come si legge una
riga di stato, e la procedura del banco con la scatola (R18) — sono in
[host/cwnetd/](../../host/cwnetd/README.md): quel README e' il posto dove
cercare "come faccio girare il loop con la scatola vera", questo e' dove
cercare gli strumenti con cui costruire e leggere le catture.

## Fixture

Ogni sessione di cattura è accompagnata da un `manifest.yaml` accanto ai suoi
file, compilato a partire da
[`manifest.template.yaml`](manifest.template.yaml). Una cattura senza
manifesto non è una prova: non si committa. Vedi il
[piano del banco di prova](../../docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md).
