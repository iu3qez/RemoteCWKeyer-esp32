---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-06T12:45:54Z"
title: "SQUEEZE_MODE_SAMPLED diverge dal K8, e tre tentativi di correzione sono falliti"
summary: "Il banco differenziale mostra 124 divergenze su 436 casi; la diagnosi è scritta, la causa probabile è nota, ma tre implementazioni di seguito hanno peggiorato il conteggio e sono state abbandonate."
keywords: ["k8", "k1el", "squeeze-mode-sampled", "issue-44", "oracolo-differenziale", "gpsim", "iambic", "inlast"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
resume_focus: "Correggere SQUEEZE_MODE_SAMPLED contro il K8, tracciando un caso divergente da un capo all'altro prima di toccare una riga."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "docs-compound-oracle-gradient"
head: "a8ccb10"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
---

# Il modo campionato diverge dal K8

Sessione del 6 settembre 2026, ripresa dall'handoff `2026-09-06_0112_k8-oracolo-eseguibile.md`.
Ha prodotto sette PR mergiate e un difetto aperto che non è stato risolto. Questo
documento serve soprattutto per quel difetto.

## Obiettivo della prossima sessione

Issue **#44**: `SQUEEZE_MODE_SAMPLED` perde elementi che il K8 manda. Il difetto è
in codice mergiato oggi con #38, non è un debito ereditato.

Decisione del maintainer, esplicita: **si segue lo standard K1EL**, non
un'approssimazione. E: si resta sul K8 come unico riferimento, «una cosa sola,
fatta bene» (Keyrama è parcheggiata in #37).

## Il metodo, che è la parte che conta

L'errore di questa sessione non è stato di conoscenza ma di metodo, ed è
documentato in
[docs/solutions/architecture-patterns/differential-oracle-count-is-not-a-gradient.md](../../../../docs/solutions/architecture-patterns/differential-oracle-count-is-not-a-gradient.md)
(in PR #52, non ancora mergiata al momento della scrittura).

**Leggilo prima di toccare `iambic.c`.** In sintesi: il conteggio delle
divergenze è un cancello, non un gradiente. Traccia **un** caso divergente da un
capo all'altro sui due lati, nostro tick per tick e riferimento istruzione per
istruzione in gpsim, prima di cambiare una riga. Raggruppa sempre le divergenze
per forma, mai leggere solo il totale.

## Cosa sappiamo della causa

Da leggere, in quest'ordine:

- **#32**, commenti dal fondo verso l'alto: la specifica verificata del
  campionamento K8, la regola del latch e le decisioni del maintainer.
- **#44**: la diagnosi del difetto e le tabelle dello sweep.
- `tools/k8/bench/FINDINGS.md`: le stesse tabelle in forma stabile.

Il nocciolo. Il K8 campiona il livello delle leve su una griglia di un'unità e
**si segna** quello che trova in `PROCLAT`. All'ultimo istante di un elemento fa
tre cose in ordine: campiona, cancella il bit del tipo appena mandato
(`morse8.asm:374` per un dit, `:409` per un dah), campiona di nuovo in `AUTOSP`
(`:536`). Il secondo campione riarma lo stesso tipo se il contatto è ancora
chiuso. La nostra implementazione fa solo la cancellazione e per il resto legge
lo stato **live** delle leve alla decisione successiva. Un fatto registrato
sopravvive al dito che si alza, una domanda posta dopo no.

Un agente su opus ha stabilito, leggendo il sorgente, che `NSAMPLE` sui bit delle
leve fa solo `BSF` e mai `BCF`, quindi **è idempotente**: i campioni di `AUTOSP` e
`SERVLOOP` non sono due osservazioni distinte. Ne segue che modellare il confine
come un campione solo è corretto, e che la domanda «uno o tre campioni» non aveva
due alternative vere. Ha anche avvertito che, una volta che entrambi i bit
possono essere accesi insieme, il tiebreak su `INLAST` diventa portante e noi non
ce l'abbiamo. `INLAST` è quindi dentro #44, non un lavoro successivo.

Il suo test di falsificazione, non ancora eseguito: se esiste un caso in cui la
divergenza segue la **fase dentro la finestra di 30 µs**, spostando un fronte di
dieci microsecondi e cambiando l'elemento emesso, allora la lettura
sull'idempotenza è sbagliata e vince l'assembly.

## Cosa è stato provato e ha fallito

Ramo locale **`wip-k8-decision-order-attempt`** (`cba48c4`), non pushato, **da
non mergiare**. Il messaggio di commit è scritto apposta per questo passaggio e
va letto: separa quello che sembra corretto da quello che non lo è.

| passo | divergenze su 436 |
|---|---|
| stato su `main` | 124 |
| dopo il riordino delle decisioni | 225 |
| dopo la correzione su `BOTH_ON` | 145 |

Alla fine 141 delle 145 erano una forma sola, «perdiamo l'elemento finale», che
non è il difetto originale. Il difetto residuo **non è stato identificato**: il
sospetto è la gestione di `BOTH_ON` in modo A, che azzera un latch che il K8
sembra tenere, ma è un sospetto e non una conclusione.

Sembrano corretti, e valgono come punto di partenza: il campionatore riscritto
sul modello di `NSAMPLE`, il campionamento a riposo (perché `SERVLOOP` campiona
liberamente quando nessun elemento è in volo, `morse8.asm:776`), il campo
`in_last_dit` distinto da `last_element`, e l'aver limitato l'ordine di decisione
K8 al solo `SQUEEZE_MODE_SAMPLED` lasciando intatti i due modi a finestra.

## Il banco

Ramo **`k8-differential-bench`** (`fae2f57`), pushato, **senza PR**. Contiene
`tools/k8/bench/` con `k8seq.py`, `sweep.py`, `ours_seq.c` e `FINDINGS.md`. Una
corsa costa circa un decimo di secondo, quindi uno sweep di 436 casi è pratico.

Vale da solo, indipendentemente dalla correzione: è quello che ha trovato il
difetto. Aprire una PR è una scelta ancora da fare.

## Stato machine-local, fragile

Percorsi assoluti, fuori dal repository e gitignorati. Sopravvivono al riavvio ma
non sono in git.

- `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/k8/` — l'originale `morse8.asm`
  (**non toccare**, sha256 `432df077a197…`), la copia patchata
  `morse8_gpasm_tb40_nosleep.asm`, il suo `morse8_tb40_nosleep.hex`
  (sha256 `e1764e099f7c…`), copie di `k8seq.py`, `sweep.py`, `ours_seq.c`, il
  risultato del confronto di timing in `k8-timing-comparison-RESULT.md`, e in
  `dj5il/` l'estrazione degli articoli DJ5IL con i PDF.
- Lo scratchpad di sessione **evapora**: tutto ciò che serviva è stato copiato
  sopra.

Le tre patch necessarie per far girare l'oracolo sono documentate in
`tools/k8/README.md`, quindi il hex è ricostruibile anche se si perde. La terza,
la sostituzione dello `SLEEP` con `GOTO SERVLOOP`, è verificata: patchato e non
patchato producono keying identico al ciclo.

## Verifica fatta

Suite host **202/202 verde**, variante piana e ASan/UBSan, su `main` e su ogni
ramo consegnato. Il confronto di timing contro il K8 è stato eseguito a 25 WPM e
a 15 WPM: ogni scarto sta dentro un tick del nostro loop, il peggiore è un
decimo di tick a 25 WPM. La tolleranza che ne è uscita è scritta in
`docs/k8-timing-tolerance.md`, mergiata.

## Il resto della sessione, già chiuso

Mergiate: #38 (il modo campionato, che è ciò che #44 corregge), #41, #42, #43,
#45, #47, #50, #51. Aperte: **#52** (il documento sul metodo), più #2 e #3 di
marzo, parcheggiate e non toccate.

Issue aperte trovate strada facendo e non ancora affrontate: **#46** (il line
editor non ha copertura host), **#48** (`usb_cdc_connected()` riporta
l'inizializzazione, non la presenza dell'host, e il ciclo di attesa in `main.c`
non attende), **#39** (arrotondamento al tick, fino al 2 % di velocità in meno,
declassata da bloccante quando si è deciso di testare solo a velocità allineate).

Nessuna issue `blocking` aperta.

## Continuazioni plausibili

La sola continuazione naturale è #44, e il primo passo non è scrivere codice: è
scegliere un caso dalla classe più numerosa, per esempio quello riprodotto in
`FINDINGS.md` (dit a 0, dah a 0,25u, dit rilasciato a 2,0u, entrambi a 2,5u, dove
il K8 manda `..-` e noi `.-`), e tracciarlo sui due lati fino a sapere quale riga
è sbagliata.

Il maintainer ha indicato di farlo in una sessione nuova a contesto pulito,
affiancata da un agente di review adversariale: quel pattern oggi ha funzionato
in modo dimostrabile, perché l'agente su opus ha rifiutato la cornice sbagliata
che gli era stata data.

Nota per chi ripiega su un subagente: **in questa build non è possibile
rimandare un messaggio a un subagente già avviato**. Un compito lungo ed
esplorativo delegato in background non è pilotabile a metà strada.
