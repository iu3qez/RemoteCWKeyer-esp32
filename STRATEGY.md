---
name: RemoteCWKeyer-esp32
last_updated: 2026-09-01
---

# RemoteCWKeyer-esp32 Strategy

## Purpose

L'operatore vuole manipolare un rig remoto con un paddle vero. Oggi l'unica
catena che parla CWNet (il protocollo del Remote CW Keyer di DL4YHF) è un PC
Windows per lato, e chi vuole rifarne un pezzo non ha un riferimento contro
cui dimostrare che è compatibile: né per il protocollo (sorgente non
compilabile, nessuna spec), né per il timing del keyer. Il progetto si è
arenato due volte per lo stesso motivo: non è mai stato impostato un metodo
di test valido e veloce.

## Positioning

Ogni comportamento che conta ha un riferimento reale e viene dimostrato
contro quello, non reso simile: il protocollo contro client/server DL4YHF
originali, il timing del keyer contro il firmware K1EL K8 (sorgente PIC ASM).
TX e RX crescono insieme perché la catena TX→RX in loop, sullo stesso
hardware o su due, è il banco di prova: niente è fatto finché non passa lì.

## Users

**Primary:** l'OM del team contest IO4A che partecipa da remoto col suo
paddle - assume il box per sedersi alla postazione CW del team senza cablare
una RS-232, senza configurare VPN e redirect audio, senza un PC Windows in
mezzo. Lato stazione non cambia nulla: Orion MkII + Thetis + server DL4YHF
sul PC di stazione.

**Secondary:** lo sviluppatore/tester - l'unico utente finché il riferimento
non è dimostrato. Il suo strumento è la console seriale, non la WebUI.

## Boundaries

- WireGuard: messa perché "c'è e costa poco", mai testata, in contest serve
  comunque la VPN sul PC. Si abbandona se fa male.
- Preset diversi dal K8 (Curtis A/B, Winkeyer, Ultimatic): best effort, non
  testati, non validati, nessun investimento.
- Niente clone di DL4YHF: compatibile col golden standard, non replica di
  tutto.
- Niente prodotto lato stazione: il server CWNet esiste solo come capo RX
  del banco di prova.
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
o non può essere dimostrata contro il riferimento (client DL4YHF, sorgente
K8).

## Key metrics

- **Conformità CWNet** - il test loop contro client/server ufficiali DL4YHF
  passa o no. Vive in `test_host` più un banco con il client Windows. È
  stata la parte più dolorosa: metrica numero uno.
- **Aderenza al K8** - decisioni esatte (quale elemento parte, quando arma
  la memoria, cosa fa lo squeeze), tempo tollerante (scarto in µs entro un
  tick di campionamento). Tolleranza scritta prima del test, non dopo. Vive
  in `test_host` contro trace del firmware K8.
  La tolleranza e la base di confronto sono scritte in
  [docs/k8-timing-tolerance.md](docs/k8-timing-tolerance.md): un tick,
  1000 µs, solo a velocità allineate al tick e con l'oscillatore emulato
  riallineato sull'elemento dit.
- **Tetto RT** - worst-case in µs di un giro del loop su Core 0
  (GPIO → iambic → stream → audio); limite 100 µs da ARCHITECTURE.md.
  Gate non ancora dimostrato: oggi non è strumentato.

## Tracks

### Banco di prova

Il metodo di test veloce che non c'è mai stato: loop contro client/server
DL4YHF, trace del K8 come oracolo, strumentazione RT, console seriale come
strumento di lavoro (log, filtri, WiFi, comandi di test, non bloccante).

_Why it serves the approach:_ senza questo "esatto" non è dimostrabile, e
il progetto si è già arenato due volte per la sua assenza.

### CWNet client

TX, poi RX, contro il server ufficiale. Un server minimo esiste solo come
capo RX del loop di test.

_Why it serves the approach:_ è il protocollo del golden standard; la
compatibilità è il prodotto.

### Keyer K8

La FSM iambic rifatta sul sorgente K1EL K8, con il criterio "decisioni
esatte, tempo tollerante".

_Why it serves the approach:_ un riferimento esatto e testabile al posto di
N preset approssimati e non verificabili.

### Postazione senza attrito

Hardware di riferimento spedito già flashato e personalizzato, Winkeyer USB
per N1MM, rete che non chiede configurazione all'OM. WebUI solo come
strumento di configurazione, ferma finché le prime tre track non tengono.

_Why it serves the approach:_ è il motivo per cui l'OM di IO4A lascia il
client Windows; senza, il riferimento dimostrato resta un esercizio.

## Brand

**One-liner:** Siamo radioamatori: è un divertimento. È pronto quando è
pronto.
