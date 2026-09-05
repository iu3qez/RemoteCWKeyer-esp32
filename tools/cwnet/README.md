# tools/cwnet — strumenti del banco di prova CWNet (U2)

Estrazione e decodifica del traffico CWNet contro il golden standard DL4YHF.
Metodo e razionale: [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md).

Nessuna dipendenza esterna: gli script sono Python 3 stdlib, i programmi C
compilano con clang/gcc. In particolare `pcap_to_stream.py` sostituisce
`tshark`, che così **non** diventa una dipendenza della CI.

## Strumenti nostri

| File | Cosa fa |
|---|---|
| `cwnet_tap.py` | Relay TCP trasparente: il client CWNet punta al tap, il tap inoltra al server e registra i due versi come byte grezzi. Cattura il loopback quando Wireshark/Npcap non lo intercetta. |
| `pcap_to_stream.py` | Estrae i flussi TCP da un pcap/pcapng e li scrive come byte grezzi, una direzione per file (solo stdlib, gestisce Ethernet/loopback/SLL/raw). |
| `cwnet_dump.c` | Decodifica un flusso grezzo: parser di frame **nostro** + codec del keying **di DL4YHF**. Diagnostico, non asserisce. |
| `diff_main.c` | Confronto esaustivo del nostro `cwnet_timestamp.c` contro `CwStreamEnc.c` di DL4YHF, tutto il dominio di ingresso. |
| `gen_synth.c` | Genera uno stream MORSE sintetico con l'encoder DL4YHF, per collaudare il decodificatore. |
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

# tap dal vivo: client -> tap -> server, un file per direzione
python3 cwnet_tap.py --listen 0.0.0.0:7355 --server <ip-server>:7355 --out sess
# oppure estrai da un pcap:
python3 pcap_to_stream.py cattura.pcapng --port 7355 --out sess
./cwnet_dump sess_1_*.bin
```

## Fixture

Ogni sessione di cattura è accompagnata da un `manifest.yaml` accanto ai suoi
file, compilato a partire da
[`manifest.template.yaml`](manifest.template.yaml). Una cattura senza
manifesto non è una prova: non si committa. Vedi il
[piano del banco di prova](../../docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md).
