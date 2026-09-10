# cwnetd — daemon di stazione CWNet

Server CWNet per PC (Linux o Mac): la manipolazione dell'OM remoto entra dal
client (la scatola, o `tools/cwnet/cwnet_send.py` per un client di prova),
esce con i tempi che il client ha mandato, il PTT segue la manipolazione
riprodotta. Il core (`components/keyer_cwnet/src/cwnet_server.c`,
`cwnet_play.c`) è lo stesso codec host-testato dalla suite `test_host`; qui
intorno c'è solo il layer POSIX (`host/platform/`), l'uscita virtuale
(`key_output.c`) e le righe di stato su stdout.

Per l'architettura, vedi [components/keyer_cwnet/CLAUDE.md](../../components/keyer_cwnet/CLAUDE.md)
e il piano [docs/plans/2026-09-08-2158-feat-station-daemon-plan.md](../../docs/plans/2026-09-08-2158-feat-station-daemon-plan.md).

## Build

```sh
cmake -S host -B host/build
cmake --build host/build
# eseguibile: host/build/cwnetd
# test (solo loopback dei socket, non il server): ctest --test-dir host/build
```

Stesse flag rigide di `test_host` (`-Wall -Wextra -Werror -Wconversion
-Wsign-conversion ...`, vedi `host/CMakeLists.txt`): un bar solo, Linux e
macOS.

## Avvio

```sh
host/build/cwnetd --listen 0.0.0.0 --port 7355
```

`--listen 0.0.0.0` ascolta su tutte le interfacce: il confine di fiducia è
la LAN o la VPN (WireGuard, `components/keyer_vpn`), non il processo — CWNet
non ha autenticazione. La porta di default, 7355, è la stessa del
riferimento e della scatola (`parameters.yaml`, `remote.server_port`).

Ctrl-C (SIGINT) o SIGTERM chiudono i client e riportano l'uscita a riposo
(tasto su, PTT spento) prima di uscire.

## Flag

```
--listen ADDR       indirizzo IPv4 di ascolto (default 0.0.0.0)
--port N            porta TCP (default 7355)
--max-clients N     client serviti insieme, max 8 (default 4)
--play-floor MS     pavimento del buffer B (default 50)
--link-ceiling MS   peak-hold oltre cui il link non e' idoneo (default 1000)
--ptt-tail MS       coda del PTT dopo l'ultimo key-up (default 100)
--ptt-lead MS       anticipo del PTT sul primo key-down, mai oltre B (default 0)
--idle MS           silenzio del titolare che rilascia la chiave (default 5000)
--over-max MS       tetto di un over (default 120000)
--handshake MS      tempo per completare il CONNECT (default 5000)
--output BACKEND    uscita di tasto e PTT: virtual (default virtual)
```

(`host/build/cwnetd --help` è la fonte, questa tabella è solo per
consultazione rapida — se divergono, fidati di `--help`.)

I default sono quelli del riferimento e della scatola (R13): `--ptt-tail
100` è "il valore della scatola" (R9), `--play-floor 50` il B minimo scelto
per assorbire il jitter della LAN/VPN senza un ritardo percepibile.

`--output` ha oggi un solo backend, `virtual`: il trasporto fisico (seriale
o GPIO verso il rig) è dietro una Decision non ancora aperta (KTD9); quando
lo sarà, un backend nuovo riempie gli stessi due puntatori a funzione di
`key_output.h` e niente sopra cambia.

## Come si legge una riga di stato

Tutto esce su stdout, una riga per evento, mai bloccante: se stdout non
tiene il passo la riga si scarta e la prossima che passa lo confessa
(`stato stdout N righe scartate`) — "niente" e "non stavi leggendo" restano
distinguibili.

Vocabolario (client, connessioni):

| Riga | Significato |
|---|---|
| `stato ascolto ADDR:PORTA max-clients N B>=X ms tetto Y ms coda Z ms lead W ms uscita BACKEND` | il daemon e' pronto; l'ultima parola prima di questa riga in stdout |
| `stato accettato client N da IP:PORTA` | TCP accettata, in attesa del CONNECT |
| `stato rifiutato da IP:PORTA: nessuno slot libero` | oltre `--max-clients`, chiusa subito (R1: l'accept non blocca mai) |
| `stato connesso client N NOME da IP:PORTA` | CONNECT completato, il client e' READY |
| `stato disconnesso client N NOME da IP:PORTA: MOTIVO` | TCP chiusa (dal peer, per timeout, per un lettore troppo lento, ...) |

Vocabolario (chiave, link, over):

| Riga | Significato |
|---|---|
| `stato chiave client N NOME` / `stato chiave libera` | chi tiene la chiave adesso (arbitrato, un titolare alla volta) |
| `stato latenza client N X ms peak Y ms` | RTT dell'ultimo PING e il suo peak-hold |
| `stato link non idoneo client N NOME: peak Y ms` | il peak-hold ha superato `--link-ceiling`: quel client non prende la chiave |
| `stato over client N NOME B X ms` | un over e' iniziato, con il B calcolato per quella sessione |
| `stato fault [client N NOME:] MOTIVO` | FAULT philosophy: timing corrotto, tasto su e si ferma (underrun, over troppo lungo, titolare muto oltre `--idle`, ...) |
| `stato eventi persi N` | il core ha prodotto piu' eventi di quanti il buffer di lettura ne tenesse: nessun fronte si perde, solo la riga descrittiva |

Uscita di tasto e PTT (`key_output.h`, backend `virtual`):

```
key 1 431839006        <- tasto giu', programmato per l'istante 431839006 (ms monotoni del processo)
key 0 431839102        <- tasto su
ptt 1 431838958         <- PTT acceso
ptt 0 431839786         <- PTT spento
```

L'istante e' quello *programmato* (B piu' la somma delle attese decodificate
dal filo), non quello in cui la riga e' stata scritta: cosi' un over si
ricostruisce dalle righe da solo, ed e' il numero che la misura del jitter
guarda dal di fuori (vedi sotto). Un fronte che non cambia stato non e' un
fronte: non genera una riga (`key_output.h`).

## Loop senza scatola

Prova rapida, senza hardware: un client Python al posto della scatola.

```sh
host/build/cwnetd --listen 127.0.0.1 --port 17355 &
python3 tools/cwnet/cwnet_send.py --host 127.0.0.1 --port 17355 --fixture first_over --verbose
```

Atteso sull'uscita virtuale (AE2, `ref_first_over`, B=50 ms di default):
quattro fronti di tasto (giu' a +50, su a +98, giu' a +146, su a +290 dallo
start dell'over) e due di PTT (acceso col primo key-down, spento 100 ms
dopo l'ultimo key-up). Dettagli e altri scenari:
[tools/cwnet/README.md](../../tools/cwnet/README.md).

## Banco con la scatola (R18)

Questo e' il passo che la CI non fa: lo esegue il maintainer, con la
scatola vera (il keyer ESP32, con un tasto o un paddle collegato) come
client. `tools/cwnet/keyer_sim.c` da' lo stimolo di riferimento —
i byte che il *client ufficiale* manderebbe per una manipolazione nota —
cosi' c'e' qualcosa di codificato contro cui confrontare i fronti veri.

### 1. Compila e avvia il daemon sul PC

```sh
cmake -S host -B host/build && cmake --build host/build
host/build/cwnetd --listen 0.0.0.0 --port 7355
```

Annota l'indirizzo IP del PC sulla stessa rete/VPN della scatola (LAN o
WireGuard — non esporre `cwnetd` su Internet, CWNet non si autentica).

### 2. Punta la scatola al daemon

Dalla console seriale della scatola (USB-CDC, `components/keyer_usb`) o
dalla sua web UI, sezione **Remote**:

```
set remote.server_host <IP del PC>
set remote.server_port 7355
set remote.username <un nome qualsiasi>
set remote.cwnet_enabled true
```

`system.callsign` e' il nominativo che finisce nel CONNECT (il campo che
`cwnet_echo.py` adesso annuncia — vedi `tools/cwnet/README.md`); se non
l'hai gia' impostato:

```
set system.callsign <il tuo nominativo>
```

Questi parametri sono `runtime_change: reboot` (`parameters.yaml`): riavvia
la scatola perche' si connetta con i nuovi valori.

### 3. Conferma la connessione

Sullo stdout di `cwnetd` deve comparire:

```
stato accettato client 1 da <IP scatola>:<porta>
stato connesso client 1 <il tuo nominativo> da <IP scatola>:<porta>
```

Se non compare: verifica che la scatola e il PC si vedano sulla rete
(ping), che la porta sia la stessa da entrambi i lati, e che
`remote.cwnet_enabled` sia effettivamente `true` dopo il riavvio (`show
remote.*` in console).

### 4. Manipola e leggi i fronti

Imposta la scatola a 25 WPM (`set keyer.wpm 25`, dot = 48 ms) e manda la
lettera "A" (di-dah) con il paddle, poi lascia il tasto fermo.

Sullo stdout di `cwnetd` deve comparire, nell'ordine, la stessa forma di
AE2/`ref_first_over` (gia' pinnata da `test_cwnet_play.c`):

```
ptt 1 <t0>          <- PTT su col primo key-down
key 1 <t0>          <- tasto giu' (il "di")
key 0 <t0+48>        <- tasto su, ~48 ms dopo (un dot a 25 WPM)
key 1 <t0+96>        <- tasto giu' (il "dah")
key 0 <t0+240>        <- tasto su, ~144 ms dopo (un dash a 25 WPM)
ptt 0 <t0+340>        <- PTT giu', 100 ms (--ptt-tail) dopo l'ultimo key-up
```

`keyer_sim.c` genera lo stesso stimolo in forma di byte (scenario A del suo
`main()`: `run("A primo over, dot 48", ...)`, che stampa `80 24 A4 3C 60`);
`decode7()` di quei byte da' le stesse attese, 48/48/144 ms. La mano non
riproduce 48.000 ms esatti come `keyer_sim.c` — il confronto e' sulla
*forma* (quattro fronti, gli intervalli vicini a 48/48/144 ms, PTT su col
primo giu' e giu' 100 ms dopo l'ultimo su), non su uno scarto in
millisecimi: quello lo fa la misura del jitter qui sotto, senza mano di
mezzo.

Per un confronto byte-esatto (non solo la forma), cattura il traffico
grezzo mentre manipoli con `tools/cwnet/cwnet_tap.py` (o un pcap con
`tools/cwnet/pcap_to_stream.py`) e decodificalo con `tools/cwnet/cwnet_dump.c`:
i byte MORSE catturati devono decodificare alle stesse attese che
`keyer_sim.c` scrive per lo scenario che hai riprodotto.

### 5. Scrivi il risultato

Il passo di banco chiude solo quando è scritto su
[#64](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/64): un
commento con cosa è stato manipolato, le righe di stdout osservate (o uno
snippet), e se la forma attesa regge.

## Misura del jitter e del tempo di scambio

`tools/cwnet/cwnet_jitter.py` fa il loop senza scatola con una sequenza
lunga (`cwnet_send.py --fixture long`, 104 elementi, la lettera "V"
ripetuta) e confronta ogni riga `key` con l'istante che porta scritto
sopra — vedi la docstring dello script per il metodo esatto e perché serve
una calibrazione fra i due orologi di processo. Non è una misura con
l'oscilloscopio sul tasto vero (quella è il passo con la scatola sopra):
è uno scarto misurato su una macchina, non una garanzia RT.

```sh
python3 tools/cwnet/cwnet_jitter.py --cwnetd host/build/cwnetd
python3 tools/cwnet/cwnet_jitter.py --cwnetd host/build/cwnetd --handover
```

### Numeri misurati

**macOS, Apple Silicon** — MacBook Pro 18,2 (Apple M1 Max), macOS 26.6.2
(Darwin 25.6.0, arm64), Python 3 di sistema, 2026-09-10, `--play-floor 50
--ptt-tail 100` (i default):

| Misura | Valore |
|---|---|
| Scarto medio (jitter), 9 run da 104 fronti | fra 0.58 e 1.46 ms |
| Scarto massimo, stesse 9 run | fra 1.4 e 4.8 ms |
| Fronti ricevuti | 104/104 in ogni run |
| Intervallo stazione: ultimo key-up -> PTT giu' (`--handover`) | 100 ms (= `--ptt-tail`, misurato su 5 run) |
| Tempo di scambio dopo la TX (client -> PTT giu' in stazione) | B + coda = 50 + 100 = **150 ms** |

Il tempo di scambio è la grandezza che interessa a chi usa il programma
originale (il ritardo fra "l'operatore remoto lascia il tasto" e "il PTT di
stazione scende"): è una somma di configurazione (B, il pavimento del
buffer, più la coda del PTT), confermata sul loop misurando esattamente
l'intervallo fra l'ultimo key-up e il PTT giù di una sessione che chiude
l'over correttamente (`ref_first_over`).

**Linux — mancante.** Questo worktree ha solo un Mac: nessun numero Linux
in questa tabella, e nessuno stimato al suo posto. Chi ha un'immagine Linux
a disposizione esegue `tools/cwnet/cwnet_jitter.py` li' e completa la
tabella; nel frattempo la lacuna resta scritta qui, non nascosta.
