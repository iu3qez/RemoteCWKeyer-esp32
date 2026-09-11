# cwnetd — daemon di stazione CWNet

Server CWNet per PC (Linux o Mac): la manipolazione dell'OM remoto entra dal
client (la scatola, o `tools/cwnet/cwnet_send.py` per un client di prova),
esce con i tempi che il client ha mandato, il PTT segue la manipolazione
riprodotta. Il core (`components/keyer_cwnet/src/cwnet_server.c`,
`cwnet_play.c`) è lo stesso codec host-testato dalla suite `test_host`; qui
intorno c'è solo il layer POSIX (`host/platform/`), l'uscita virtuale
(`key_output.c`) e le righe di stato su stdout. Stato e fronti escono da due
descrittori diversi, e per un buon motivo: vedi *Due uscite* qui sotto.

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
--play-floor MS     pavimento del buffer B (default 100)
--link-ceiling MS   peak-hold oltre cui il link non e' idoneo (default 1000)
--ptt-tail MS       coda del PTT dopo l'ultimo key-up (default 100)
--ptt-lead MS       anticipo del PTT sul primo key-down, mai oltre B (default 0)
--idle MS           silenzio del titolare col tasto su che rilascia la chiave
                    (default 5000; col tasto giu' decide il PING)
--over-max MS       tetto di un over (default 120000)
--handshake MS      tempo per completare il CONNECT (default 5000)
--out-cap BYTE      byte non inviati per client oltre i quali lo chiudo
                    (default 16384, min 256, max 16777216)
--output BACKEND    uscita di tasto e PTT: virtual (default virtual)
--edges DEST        descrittore dei fronti: 'stderr' o un file (default stderr)
```

(`host/build/cwnetd --help` è la fonte, questa tabella è solo per
consultazione rapida — se divergono, fidati di `--help`. `--help` stampa i
default leggendoli dalla stessa struttura che il programma usa, quindi non
può divergere dal comportamento.)

I default sono quelli del riferimento e della scatola (R13): `--ptt-tail
100` è "il valore della scatola" (R9), `--play-floor 100` il B minimo scelto
per assorbire il jitter della LAN/VPN senza un ritardo percepibile.

**`--idle` non tocca un tasto giù.** Fra un elemento e l'altro e fra un over
e l'altro libera una chiave che nessuno sta usando; sotto un tasto tenuto
giù non ha voce, altrimenti taglierebbe l'accordatura dopo cinque secondi.
Chi decide lì è il PING: risponde il programma, non la mano dell'operatore,
quindi continua a rispondere per tutta l'accordatura e smette quando il
client muore. Tre PING senza risposta chiudono quel client e la chiave si
rilascia con tasto su e PTT giù alla coda (R8, R16).

**`--out-cap` è una rete di sicurezza, non una prestazione.** Il server manda
a un client qualche decina di byte ogni due secondi: 16 KiB sono minuti di
arretrato. Un peer che non legge da tanto non torna, e tenergli i byte costa
solo agli altri, che aspettano il loro PING dietro di lui. Superato il tetto
quel client si chiude, il loop non rallenta.

`--output` ha oggi un solo backend, `virtual`: il trasporto fisico (seriale
o GPIO verso il rig) è dietro una Decision non ancora aperta (KTD9); quando
lo sarà, un backend nuovo riempie gli stessi due puntatori a funzione di
`key_output.h` e niente sopra cambia.

## Due uscite, e perché

Lo **stato** esce su stdout, non bloccante: se il lettore non tiene il passo
la riga si scarta e la prossima che passa lo confessa. È la regola giusta per
una diagnostica, perché una riga persa costa una riga.

I **fronti** no. Escono sul descrittore di `--edges` (`stderr`, o un file),
che non ne scarta mai uno (R11, KTD9). Il motivo è cosa sono quelle righe:
sono la misura del jitter. Una traccia che perde in silenzio proprio i fronti
che sta misurando non è una misura peggiore, è una misura sbagliata — e la
perderebbe esattamente quando il sistema è carico, cioè quando il numero
conta.

Le due regole — «non scarta mai» e «il loop non si ferma mai» — non possono
valere entrambe contro un lettore che ha smesso di drenare. Qui vince R11, e
il costo si rende visibile invece di nasconderlo:

- **Un file non può fermare il loop.** `write(2)` su un file regolare non
  restituisce mai `EAGAIN` e non ha un lettore da aspettare. È il descrittore
  da usare quando i numeri contano: `--edges fronti.log` (apertura in
  append, così un riavvio aggiunge alla traccia invece di cancellarla).
- **Una pipe o un terminale sì.** La scrittura viene ritentata attraverso
  `EINTR`, le scritture parziali e `EAGAIN` (`poll()` per `POLLOUT`), quindi
  la riga arriva comunque; ma ogni millisecondo di attesa ferma il loop. Una
  pipe che nessuno legge non costa niente finché il suo buffer non si riempie
  (16-64 KiB, qualche migliaio di fronti) e da lì in poi ferma la stazione.
  Il daemon lo dice mentre succede — `stato uscita fronti (...) non drena: N
  ms e aspetto` — e a fine sessione stampa il totale delle attese.
- **Le flag del descrittore non si toccano mai.** `stderr` può condividere la
  open file description con il nostro stdout non bloccante (la `2>&1` della
  shell fa esattamente questo): togliere `O_NONBLOCK` da uno lo toglierebbe
  all'altro, rendendo bloccanti le righe di stato senza che si veda. Gestire
  `EAGAIN` funziona qualunque cosa si sia ereditato.
- **L'unica eccezione è l'arresto.** Dopo SIGINT o SIGTERM l'attesa per
  fronte è limitata a un secondo: un descrittore che nessuno drena non deve
  rendere il daemon impossibile da chiudere se non con SIGKILL. I fronti
  lasciati indietro finiscono nel conteggio `persi` dell'ultima riga.

`--edges stdout` viene rifiutato all'avvio: è l'unico descrittore che non può
dare ai fronti la separazione che R11 chiede.

## Come si legge una riga di stato

Lo stato esce su stdout, una riga per evento, mai bloccante: se stdout non
tiene il passo la riga si scarta e la prossima che passa lo confessa
(`stato stdout N righe scartate`) — "niente" e "non stavi leggendo" restano
distinguibili. I fronti non sono qui: sono su `--edges` (vedi *Due uscite*).

Vocabolario (client, connessioni):

| Riga | Significato |
|---|---|
| `stato ascolto ADDR:PORTA max-clients N B>=X ms tetto Y ms coda Z ms lead W ms out-cap C byte` | il daemon e' pronto, con la configurazione che ha davvero |
| `stato uscita BACKEND fronti DEST` | dove finiscono i fronti (riga a parte: un path lungo non deve troncare la configurazione) |
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
| `stato byte in ritardo client N NOME: B byte, M ms in totale` | un byte e' arrivato dopo la scadenza del fronte che portava: l'elemento e' uscito piu' lungo di quanto e' stato manipolato, e il link sta scivolando. Cumulativi, quindi due righe a distanza dicono *quanto in fretta* |
| `stato fault [client N NOME:] MOTIVO` | FAULT philosophy: tasto su e si ferma (titolare sparito a meta' over — TCP chiuso o tre PING senza risposta —, over troppo lungo, titolare muto oltre `--idle` col tasto su) |
| `stato eventi persi N` | il core ha prodotto piu' eventi di quanti il buffer di lettura ne tenesse: nessun fronte si perde, solo la riga descrittiva |
| `stato uscita fronti (DEST) non drena: N ms e aspetto` | il descrittore dei fronti ha smesso di prendere byte e il loop e' fermo li' da N ms (vedi *Due uscite*) |
| `stato uscita fronti (DEST): A attese per M ms, E errori, P persi` | il consuntivo a fine sessione: `P` diverso da zero e' l'unico caso in cui un fronte non e' stato scritto, e succede solo dopo un SIGINT |

**Ne' una FIFO vuota ne' un tasto tenuto giu' sono un guasto.** La FIFO vuota
e' lo stato normale di un over dal vivo (R8): ogni byte arriva circa B ms
prima della propria scadenza, e ogni elemento piu' lungo di B la svuota. Un
tasto giu' con niente che arriva e' l'accordatura, che a bassa potenza e'
procedura: il motore non lo solleva mai da se', perche' il silenzio del
keying non distingue chi tiene giu' da chi e' caduto. Quella distinzione la
fa il PING, e il fault che si vede col tasto giu' e' il titolare dichiarato
morto. Il segnale che arriva *prima*, quando il link comincia a scivolare ma
i byte ancora arrivano, e' `stato byte in ritardo`.

Uscita di tasto e PTT (`key_output.h`, backend `virtual`) — **non su stdout**:
sul descrittore di `--edges`, che di default e' `stderr`:

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
fronte: non genera una riga (`key_output.h`). Nessuna di queste righe viene
mai scartata — e' l'intero motivo per cui hanno un descrittore loro.

## Loop senza scatola

Prova rapida, senza hardware: un client Python al posto della scatola.

```sh
host/build/cwnetd --listen 127.0.0.1 --port 17355 --edges /tmp/fronti.log &
python3 tools/cwnet/cwnet_send.py --host 127.0.0.1 --port 17355 --fixture first_over --verbose
cat /tmp/fronti.log
```

Atteso sull'uscita virtuale (AE2, `ref_first_over`, B=100 ms di default):
quattro fronti di tasto (giu' a +100, su a +148, giu' a +196, su a +340
dall'arrivo del primo byte) e due di PTT (acceso col primo key-down, spento
100 ms dopo l'ultimo key-up). Senza `--edges` finiscono su stderr, cioe' sul
terminale insieme allo stato. Dettagli e altri scenari:
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
host/build/cwnetd --listen 0.0.0.0 --port 7355 --edges /tmp/fronti.log
```

(`--edges` su un file: cosi' i fronti restano leggibili anche quando lo
stato scorre, e un file non puo' fermare il loop di temporizzazione.)

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

Nel file di `--edges` deve comparire, nell'ordine, la stessa forma di
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

(Lo script legge i fronti da `stderr` del daemon, che unisce alla stessa
pipe dello stato: è il default di `--edges`, e le due epoche monotone dei
due processi vengono calibrate sul primo fronte — vedi la docstring.)

### Numeri misurati

**macOS, Apple Silicon** — MacBook Pro 18,2 (Apple M1 Max), macOS 26.6.2
(Darwin 25.6.0, arm64), Python 3 di sistema, 2026-09-10, `--play-floor 100
--ptt-tail 100` (i default):

| Misura | Valore |
|---|---|
| Scarto medio (jitter), 9 run da 104 fronti | fra 0.39 e 0.64 ms |
| Scarto massimo, stesse 9 run | fra 1.2 e 4.7 ms |
| Fronti ricevuti | 104/104 in ogni run |
| Intervallo stazione: ultimo key-up -> PTT giu' (`--handover`) | 100 ms (= `--ptt-tail`, misurato su 5 run) |
| Tempo di scambio dopo la TX (client -> PTT giu' in stazione) | B + coda = 100 + 100 = **200 ms** |

Il tempo di scambio è la grandezza che interessa a chi usa il programma
originale (il ritardo fra "l'operatore remoto lascia il tasto" e "il PTT di
stazione scende"): è una somma di configurazione (B, il pavimento del
buffer, più la coda del PTT), confermata sul loop misurando esattamente
l'intervallo fra l'ultimo key-up e il PTT giù di una sessione che chiude
l'over correttamente (`ref_first_over`).

**Erano 150 ms, adesso sono 200.** Il pavimento di B è passato da 50 a
100 ms, e questo numero lo segue: sono i 100 ms in più che si pagano per non
tagliare un elemento quando la rete fa un salto. Chi ha un link stabile —
LAN, o una VPN su fibra — lo riporta dov'era con `--play-floor 50`, e riavrà
150 ms; è una scelta di configurazione, non un limite del programma. Il
pavimento non è il ritardo: se il peak-hold del titolare è più alto, B è
quello, e il tempo di scambio sale di conseguenza.

**Linux, x86-64 su metallo** — `sf-B450M-DS3H-V2` (AMD Ryzen 7 5700G),
Linux 7.0.0-31-generic x86_64, 2026-09-12, gli stessi default. E' la
macchina di stazione, avviata da un disco Ubuntu:

| Misura | Valore |
|---|---|
| Scarto medio (jitter), 3 run da 104 fronti | fra 0.25 e 0.47 ms |
| Scarto massimo, stesse 3 run | fra 0.62 e 1.07 ms |
| Fronti ricevuti | 104/104 in ogni run |
| Intervallo stazione: ultimo key-up -> PTT giu' (`--handover`) | 100 ms (= `--ptt-tail`) |
| Tempo di scambio dopo la TX | B + coda = 100 + 100 = **200 ms** |

**Misurati a macchina scarica**, senza altro carico in esecuzione. Nessuno
ha misurato cosa succede sotto carico, e quello e' il caso che conta per una
stazione che fa anche altro: chi ci mette sopra un browser, una cattura o un
backup rifaccia la misura invece di fidarsi di questa riga.

Con quella riserva: piu' fedele del Mac, e non di poco, perche' il massimo
peggiore qui sta sotto il migliore di la'. Due macchine sole non fanno una
legge, e nessuna delle due e' una misura all'oscilloscopio sul tasto vero —
quella resta il passo di banco con la scatola.

Una nota sul metodo, perche' cambia fra i due sistemi: lo script si calibra
sul primo fronte e misura la deriva da li'. Su macOS **deve** farlo, perche'
`time.monotonic()` di Python e `CLOCK_MONOTONIC` del daemon non condividono
l'epoca; su Linux la condividono, quindi li' la calibrazione non serve e non
nasconde niente. I numeri delle due tabelle restano confrontabili perche'
misurano la stessa cosa, la deriva fronte per fronte.
