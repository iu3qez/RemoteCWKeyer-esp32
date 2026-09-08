---
name: RemoteCWKeyer-esp32
last_updated: 2026-09-08
---

# RemoteCWKeyer-esp32 Strategy

## Purpose

L'operatore vuole manipolare un rig remoto con un paddle vero. Oggi l'unica
catena che parla CWNet (il protocollo del Remote CW Keyer di DL4YHF) è un PC
Windows per lato, e chi vuole rifarne un pezzo non ha un riferimento contro
cui dimostrare che è compatibile: né per il protocollo (sorgente non
compilabile, nessuna spec), né per il timing del keyer. Il programma DL4YHF
ha uno scopo diverso dal nostro e non si può piegare: Borland, Windows only,
codice della GUI non disponibile, il carrozzone CI-V al seguito, scelte di
stazione opinabili (PTT tenuto 500 ms). Il progetto si è arenato due volte
per lo stesso motivo: non è mai stato impostato un metodo di test valido e
veloce.

## Positioning

Ogni comportamento che conta ha un riferimento reale e viene dimostrato
contro quello, non reso simile: il filo CWNet contro client e server DL4YHF
originali, il keyer contro il K1EL K8 eseguito: input umano → output K8,
riproducibile, fino a 40 WPM. Il K8 è riferimento del feeling, non
dell'implementazione: i limiti di un PIC12 del 1998 non sono i nostri.
Il programma DL4YHF è riferimento del protocollo, non del prodotto: il
prodotto è la coppia scatola + server nostro, e le scelte di stazione (PTT,
cessione della chiave, buffer di riproduzione) sono nostre, prese una volta
sola perché possediamo i due capi. CWNet trasporta anche audio, CI-V e
spettro: noi lo usiamo per la manipolazione e per ciò che le serve; il resto
della stazione non è impegnato a passare da lì.
TX e RX crescono insieme perché la catena TX→RX in loop, sullo stesso
hardware o su due, è il banco di prova: niente è fatto finché non passa lì.

## Users

**Primary:** l'OM del team contest IO4A che partecipa da remoto col suo
paddle - assume il box per sedersi alla postazione CW del team senza cablare
una RS-232, senza configurare VPN e redirect audio, senza un PC Windows in
mezzo. Lato stazione: Orion MkII + Thetis, e il server nostro su un PC Linux
o Mac al posto del programma DL4YHF.

**Secondary:** lo sviluppatore/tester - l'unico utente finché il riferimento
non è dimostrato. Il suo strumento è la console seriale, non la WebUI.

## Boundaries

- WireGuard: messa perché "c'è e costa poco", mai testata, in contest serve
  comunque la VPN sul PC. Si abbandona se fa male.
- Preset diversi dal K8 (Curtis A/B, Winkeyer, Ultimatic): la logica resta
  configurabile per assi e non si lega al K8, ma un valore che nessun
  riferimento eseguibile prova non si popola. Best effort, nessun investimento.
- Sopra i 40 WPM il K8 non è più riferimento: le finestre configurabili sono
  best effort, senza metrica.
- Niente clone di DL4YHF: compatibile sul filo, non replica di tutto.
- Audio, CI-V e spettro dentro CWNet: nessun impegno. Il server nostro
  implementa i comandi che la manipolazione richiede; il resto della stazione
  passa da dove passa oggi.
- Niente scatola come server: per IO4A il server è un daemon su PC di
  stazione. Una scatola con doppia personalità client/server è un repurpose
  utile, anche per i test, ma non ora.
- Niente OTA ora: aggiornamento via flasher web (repo separato) + USB. Se
  arriva dopo, meglio.
- WebUI: nessun investimento finché banco di prova, CWNet e K8 non tengono.
- Il log seriale su ESP32 blocca il real time: nessun log bloccante sul
  path RT, mai.

Un boundary vieta di **costruire**, non di **ricordare**. Aprire una issue su
qualcosa che sta fuori dai confini non è investimento: è il modo di non
riscoprirlo daccapo fra sei mesi, e di sapere cosa aspetta quando il confine
si sposterà. Quello che il boundary esclude è la schedulazione - il lavoro
parcheggiato non passa davanti a banco di prova, CWNet e K8.

Il tracker registra anche ciò che non faremo adesso. Un backlog che contiene
solo il lavoro autorizzato non è disciplina: è amnesia.

_Resist a change when:_ l'unico argomento è "c'è e costa poco aggiungere",
non può essere dimostrata contro il riferimento (client DL4YHF, K8
eseguito), o riproduce una scelta di stazione del programma DL4YHF solo
perché il riferimento la fa.

## Key metrics

- **Conformità CWNet** - il test loop contro client/server ufficiali DL4YHF
  passa o no, in entrambi i ruoli: il client della scatola contro il server
  DL4YHF, il daemon nostro contro il client DL4YHF. Vive in `test_host` più
  un banco con il programma Windows. È stata la parte più dolorosa: metrica
  numero uno.
- **Feeling del K8** - su un corpus di manipolazioni reali fino a 40 WPM,
  la sequenza di elementi nostra coincide con quella del K8 eseguito, stabile
  sotto la fase dello stimolo: passa o no. Vive nel repo della logica keyer,
  come suo gate di CI; qui si legge quale commit pinnato lo ha passato.
  Il tempo resta tollerante come scritto in
  [docs/k8-timing-tolerance.md](docs/k8-timing-tolerance.md): un tick,
  1000 µs, a velocità allineate al tick.
- **Tetto RT** - worst-case in µs di un giro del loop su Core 0
  (GPIO → iambic → stream → audio); limite 100 µs da ARCHITECTURE.md.
  Gate non ancora dimostrato: oggi non è strumentato.

## Tracks

### Banco di prova

Il metodo di test veloce che non c'è mai stato: loop contro client/server
DL4YHF, cattura delle leve dalla scatola per il corpus del keyer,
strumentazione RT, console seriale come strumento di lavoro (log, filtri,
WiFi, comandi di test, non bloccante).

_Why it serves the approach:_ senza questo "esatto" non è dimostrabile, e
il progetto si è già arenato due volte per la sua assenza.

### CWNet, i due capi

Client sulla scatola: TX, poi RX, contro il server DL4YHF. Server nostro
come daemon su PC di stazione, Linux o Mac, contro il client DL4YHF, con le
policy di stazione decise da noi. L'echo server del banco resta il capo RX
del loop di test ed è il seme del daemon.

_Why it serves the approach:_ il filo è del golden standard, le policy di
stazione sono nostre: possedere i due capi è il modo di deciderle una volta
sola e di dimostrarle in loop.

### Keyer

La logica del keyer vive in un repo suo, consumato qui come submodule a
commit pinnato. Interfaccia (`iambic.h`, `sample.h`) e motore RT sono di
questo repo e da lì non si toccano; il resto è suo. Qui si fa il bump e si
fornisce la cattura delle leve.

_Why it serves the approach:_ il feeling del K8 si dimostra con uno
strumento suo, senza fermare CWNet e senza che CWNet lo fermi.

### Postazione senza attrito

Hardware di riferimento spedito già flashato e personalizzato, Winkeyer USB
per N1MM, rete che non chiede configurazione all'OM. WebUI solo come
strumento di configurazione, ferma finché le prime tre track non tengono.

_Why it serves the approach:_ è il motivo per cui l'OM di IO4A lascia il
client Windows; senza, il riferimento dimostrato resta un esercizio.

## Brand

**One-liner:** Siamo radioamatori: è un divertimento. È pronto quando è
pronto.
