# RemoteCWKeyer-esp32 — Memory Index

## Strategia (2026-09-01)
[STRATEGY.md](../STRATEGY.md) è l'ancora: ogni comportamento che conta si dimostra
contro un riferimento reale (protocollo CWNet → client/server DL4YHF; timing keyer
→ sorgente K1EL K8), non "reso simile". Il box è **lato operatore** (postazione CW
remota per il team IO4A); lato stazione non si costruisce nulla.

Track attive, in ordine: **banco di prova** → **CWNet client** → **keyer K8** →
**postazione senza attrito**. Prima di proporre lavoro, leggi le Boundaries:
WireGuard è abbandonabile, i preset diversi dal K8 sono best effort non validati,
niente OTA per ora, WebUI ferma finché le prime tre track non tengono.

Frase-test: resistere a una modifica quando l'unico argomento è "c'è e costa poco
aggiungere", o quando non è dimostrabile contro il riferimento.

## Issue bloccanti
Una issue GitHub etichettata `blocking` ferma il lavoro che ne dipende, punto. Non si pianifica
intorno, non si sostituisce con un'assunzione, non si procede marcando il lavoro "provvisorio".
Regola completa in [CLAUDE.md](../CLAUDE.md#a-blocking-issue-blocks), fra i Critical Constraints.

Aperte ora: #7 (sequenza nota e definizione di "identico") e #8 (provenienza delle catture).
Entrambe bloccano la sessione zero del banco CWNet.

## Definition of done
Vedi [CLAUDE.md](../CLAUDE.md#definition-of-done). In breve: test host verdi in
entrambe le varianti CI (plain + ASan/UBSan), mai skippare un test per arrivarci,
e chi tocca `keyer_cwnet/` o `keyer_iambic/` porta un test contro il riferimento.

## Feature Status (2026-05-07)
See [feature-status.md](feature-status.md) for full categorized feature checklist.

## Code Quality Notes
See [code-quality.md](code-quality.md) for stale code, dead stubs, and cleanup items.
