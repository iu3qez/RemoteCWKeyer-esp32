---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-07T18:21:04Z"
title: "La logica keyer vive in Esp32KeyerTest; #44 è quattro meccanismi e un corpus che non c'è"
summary: "Sessione che ha tracciato il caso divergente di #44, fatto smentire metà della diagnosi da una review, cambiato la strategia (K8 = feeling, corpus black-box) e spostato la FSM in un repo suo consumato come submodule."
keywords: ["esp32keyertest", "submodule", "k8", "issue-44", "corpus", "black-box", "strategy", "feeling", "deploy-key", "axes"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
resume_focus: "Sul repo Esp32KeyerTest: la ristrutturazione ad assi di issue #1 (senza cambio di comportamento) e, sul principale, la cattura delle leve (#54) che sblocca il corpus (#8)."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "3abb071"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/k8-oracolo-handoff-resume-6f0342"
---

# La logica keyer vive in Esp32KeyerTest

Sessione del 6-7 settembre 2026, ripresa da `2026-09-06_1245_k8-sampled-mode-diverge.md`.
È partita per correggere #44 e ha finito per cambiare dove e come quel lavoro si fa.
Chi riprende deve prima sapere questo: **la FSM iambic non è più in questo repo.**

## Dove sono le cose adesso

- **[iu3qez/Esp32KeyerTest](https://github.com/iu3qez/Esp32KeyerTest)** (privato; clone
  locale `~/Developer/Esp32KeyerTest`, machine-local): la radice è il componente ESP-IDF.
  Dentro: `include/`, `src/`, `test_host/` (26 test, `interface/sample.h` è una copia
  congelata), `tools/k8/` (oracolo + banco come scheletro + `trace_decode.py`),
  `STRATEGY.md` e `CLAUDE.md` propri, i tre template di issue. Otto issue aperte.
- **Questo repo** lo consuma come submodule in `components/keyer_iambic`, pinnato a
  `05d183f` (il repo keyer è avanti di tre commit, tutti documentali: il bump non è urgente).
  PR **#53** mergiata in `8dea26f`, CI verde su entrambi i job. La suite host qui è 176 test;
  176 + 26 = i 202 di prima.
- **CI e submodule privato**: `GITHUB_TOKEN` non lo legge. I workflow fanno checkout
  normale poi `insteadOf` verso ssh e `submodule update` con la deploy key in sola lettura
  (id `162463041` sul repo keyer; secret `ESP32KEYERTEST_DEPLOY_KEY` qui). La chiave privata
  non esiste su disco. **Non "semplificare" in `ssh-key:` di `actions/checkout`**: con una
  chiave impostata non riscrive gli URL dei submodule (`git-auth-helper.ts:77`, letto).

## Le decisioni del maintainer, in ordine

Tutte del maintainer, esplicite, in chat; scritte in `STRATEGY.md` di qua (commit `b883403`)
e in `Esp32KeyerTest/STRATEGY.md`:

1. **Il K8 è riferimento del feeling, non dell'implementazione.** Feeling = input umano →
   output K8, riproducibile, **fino a 40 WPM**. I limiti di un PIC12 (finestra di polling da
   14 µs) non sono i nostri. Sopra 40 WPM nessun riferimento, nessuna metrica.
2. **Il sorgente nomina, la black box giudica.** Il sorgente K8 sceglie gli assi di
   configurazione e controlla la copertura; l'atteso viene solo dall'oracolo eseguito su un
   **corpus di manipolazioni reali** (errori inclusi); una divergenza conta solo se stabile
   sotto la fase. Tolleranza nel comparatore, mai nel modello.
3. **Logica universale per assi**: un comportamento entra come valore su un asse, mai come
   `if` su un modello di keyer; il K8 è un preset. Un asse si apre quando un riferimento reale
   sta altrove; valori non provati non si popolano.
4. Due track nel repo keyer: **Banco** (vince) e **FSM ad assi**.
5. Senza corpus si **ristruttura** (comportamento invariato, test verdi) ma non si cambia il
   comportamento; la **copertura è un cancello**; la **cattura la fa il principale**.
6. Repo nuovo privato, consumato come submodule (non branch, non copia vendorizzata).

Scelte **mie**, non del maintainer: radice del repo = componente; suite spezzata 176/26;
`FINDINGS.md` non migrato (numeri superati); deploy key invece di PAT; #32 lasciata qui e
chiusa dal sweep; le formulazioni dei corpi delle issue.

## Cosa ha stabilito il trace, e cosa ha smentito la review

Tutto in `Esp32KeyerTest/tools/k8/bench/README.md` e nel corpo di **Esp32KeyerTest#1**
(ex #44, riscritta). In breve: quattro meccanismi al confine di elemento (riarmo dello stesso
tipo dopo `BCF` a `morse8.asm:374` via `AUTOSP :536`; tiebreak `INLAST` in `CHK_SINGLE`;
`squeeze_seen` armato a metà elemento con cancellazione irraggiungibile in SAMPLED; ordine
delle decisioni con `TOGGLE` prima della memoria). **Solo il primo ha prova stabile alla
fase** (8 casi). I «124 su 436» sono ritirati: l'oracolo gira solo in Mode B e lo sweep
confrontava anche il nostro Mode A; 32 stimoli malformati; 26 delle 50 divergenze reali si
spostano con 4,7 µs. Il caso rappresentativo dell'handoff precedente sta 2 cicli dentro una
finestra da 56.

La review adversariale (agente su `opus`, mandato di falsificare) è il pattern che ha
funzionato: due affermazioni mie su sei smentite. Ripetere.

## Stato del tracker

Principale: #32 **chiusa** su evidenza (sweep del 2026-09-07); #26 `narrowed` — il
maintainer ha ristretto i LED a un'indicazione binaria in un commento, il corpo non è
aggiornato; #54 filata (cattura). Le altre aperte sono CWNet/USB/console, invariate.

Esp32KeyerTest: #1 (ex #44), #2 (ex #39), #3/#4 parcheggiate (ex #37/#35), #5 modello host
come cache di gpsim, #6 oracolo in Mode A, #7 patch `SLEEP` da verificare sul sorgente
(**non** verificata da me: la review dice che il wake da `SLEEP` passa da `CLRF PROCLAT`
`:774`), #8 corpus + comparatore. Catena: **#54 → #8 → #1**. Nessuna `blocking`.

## Debito leggero, in `.claude/code-quality.md`

Blocchi `treecode` di `test_host/CLAUDE.md` e `keyer_core/CLAUDE.md` da risincronizzare con
`map-tree`; ramo remoto `k8-differential-bench` da cancellare quando non serve. Il principio
di universalità non è in `ARCHITECTURE.md` di qua: sta in `Esp32KeyerTest/CLAUDE.md` e
`STRATEGY.md`, che è dove la FSM vive ora — se serva anche qui è del maintainer.

## Stato machine-local, fragile

- `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/k8/`: `morse8.asm` originale (sha256
  `432df077…`, non toccare), copia patchata, `morse8_tb40_nosleep.hex` (`e1764e09…`),
  log gpsim. Il hex si riassembla dal README: verificato identico.
- Rami locali `wip-k8-decision-order-attempt` (tentativo abbandonato, diagnosi superata ma
  da tenere) e `k8-differential-bench`.
- Lo scratchpad di sessione evapora: quello che serviva (decoder, script gpsim) è nel repo keyer.

## Verifica fatta

CI verde su `8dea26f` (host-tests plain e ASan/UBSan, firmware-build esp32s3) e sul repo
keyer al primo push. Localmente 176/176 + 26/26 in entrambe le varianti, `sample.h`
identica. `firmware-build` **non** eseguibile su questa macchina: niente ESP-IDF.

## Continuazione

Una sola strada, su due repo:

- **Esp32KeyerTest #1, prima metà**: ristrutturare `iambic.c` ad assi senza cambiare
  comportamento, suite verde — l'unico lavoro sulla FSM autorizzato senza corpus. `ce-plan`
  di là, con la review adversariale prima di scrivere.
- **Principale #54**: la cattura sulla scatola (serve hardware). Sblocca #8, che sblocca la
  seconda metà di #1.
- Indipendenti e piccole: #6 (Mode A nell'oracolo), #7 (verifica della patch `SLEEP`), #5.
