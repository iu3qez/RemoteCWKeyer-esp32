---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-09T20:54:10Z"
title: "La regola del colore, il verdetto sulla chiave, la potatura dei LED"
summary: "Sessione 9 settembre 2026: #73 (voce docs/solutions sul seqlock a due fence) e #74 (nove stati LED diventati cinque situazioni sotto la regola verde = il tuo CW esce, verdetto can_transmit, riga KEY nella Web UI) mergiati, #26 chiusa sull'evidenza; sweep senza altre chiusure; prossimo passo il lato stazione (#64, #68) con la GUI ferma su #65."
keywords: ["led", "led_render", "colour-rule", "on-air", "off-air", "can_transmit", "key-holder", "tx_info", "seqlock", "fence", "docs-solutions", "concepts", "issue-26", "issue-64", "issue-65", "issue-68", "station-daemon", "esp-idf", "eim"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
resume_focus: "Il lato stazione: #64 daemon e #68 client host, ripartendo da 2026-09-08_1000_station-daemon-start.md e dal lavoro non committato nel worktree station-daemon-startup-03165a (il piano del daemon dell'8 settembre). La GUI resta ferma su #65 blocking."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "75dfb28"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
---

# La regola del colore, il verdetto sulla chiave, la potatura dei LED

Ripresa da `2026-09-08_1939_stream-fences-and-key-holder.md`, che lasciava in coda una
voce `docs/solutions/` sulle due fence e #26 narrowed. Entrambe sono chiuse. Tutto è su
`main` a `75dfb28`, due PR mergiate dal maintainer, CI verde su dieci gambe compresa
`firmware-build`. Suite host da 218 a 228. Niente provato su hardware: i LED nuovi non li
ha ancora visti nessuno accesi.

## Cosa esiste adesso, e dove

- **Il seqlock ha due metà** (PR #73). `docs/solutions/architecture-patterns/a-seqlock-has-two-halves-and-needs-two-fences.md`:
  cosa un load acquire e uno store release *non* ordinano, i conteggi strappati per
  piattaforma, il test che rende osservabile lo strappo. `CONCEPTS.md` ha ora la voce
  *Keying stream* (il ring in RAM) distinta da *MORSE keying stream* (il filo CWNet), con
  l'ambiguità registrata in coda. `CODING_STYLE.md`, guida ai memory order: i tag ordinano
  un lato solo, per l'altro serve la fence.
- **La regola del colore** (PR #74, #26 chiusa). Da leggere, non da riscrivere: il corpo di
  #26 (decisione del maintainer del 2026-09-09, che sostituisce la narrowing del 5) e
  l'intestazione di `components/keyer_led/include/led_render.h`. Nove stati sono cinque
  situazioni, una per colore, in `led_situation_t`; le animazioni sono eventi
  (`led_notify()`, tre lampi nel colore su cui atterrano); il render è puro in
  `components/keyer_led/src/led_render.c`, `led.c` è solo il guscio RMT. Il mapping da
  WiFi e CWNet alla situazione è `main/bg_task.c:66-84`, `led_situation_now()`, riletto a
  ogni tick. Otto test in `test_host/test_led.c`; quello che conta è
  `test_led_green_belongs_to_on_air_and_to_nothing_else`.
- **Il verdetto sulla chiave**. `cwnet_client_can_transmit()` in
  `components/keyer_cwnet/src/cwnet_client.c`: READY, TRANSMIT concesso, chiave libera o
  nostra, cioè quello che il server del riferimento fa con un frame MORSE
  (`CwNet.c:2875-2902`). UNKNOWN vale «no»: l'annuncio segue l'eco del CONNECT nella
  stessa raffica (cattura sessione 12, offset 169). Wrapper `cwnet_socket_can_transmit()`,
  campo `can_transmit` in `/api/system`, riga KEY in `System.svelte:197-199` con
  `keyLabel()` a riga 21. Test pinnati sui fixture della cattura in
  `test_host/test_cwnet_client.c`, i due `test_client_can_transmit_*`.

## Decisioni, e di chi sono

- **Del maintainer**: la regola «verde vuol dire che il tuo CW esce, ogni altro colore vuol
  dire che non esce e il colore dice perché»; le animazioni sono eventi; l'overlay dei
  paddle resta perché serve in debug. Tutto in chat il 2026-09-09, registrato nel corpo di
  #26.
- **Mie, approvate implicitamente col merge**: l'overlay porta posizione e luminosità nel
  colore della situazione, mai un colore suo (un overlay verde su base rossa smentirebbe la
  regola mentre si manipola); lo squeeze è tutta la barra accesa, il magenta esce dal
  vocabolario; WiFi disabilitato in config è verde, non giallo, perché la scatola è il
  keyer locale che le è stato chiesto di essere; il lampo rosso da mezzo secondo prima del
  giallo è tolto.
- **Alternativa scartata**: riservare alcuni dei sette LED al link. È il codice da
  ricordare che il maintainer aveva escluso il 5 settembre.

## Cosa ha insegnato la sessione

- **L'overlay dei paddle è uno strumento di diagnosi, con un limite preciso.** `bg_task`
  legge i pin per conto suo (`hal_gpio_read_paddles()`, `gpio_get_level` grezzo, a monte
  del debounce da 5 ms e della FSM). LED acceso e CW muto: guasto a valle nel software. LED
  spento: il contatto non arriva al pin. A 100 Hz dice *se* il contatto chiude, non quanto
  è pulito: il rimbalzo è invisibile.
- **Un handoff che dice «X non c'è su questa macchina» va verificato meglio di così.** Il
  precedente lasciava intendere che il firmware si compilasse solo in CI; era falso, e l'ho
  ripetuto in PR #74. Tre indizi negativi (`which`, `$IDF_PATH`, `~/esp`) non bastavano.
- **Un seqlock ha due metà** è ora nel corpus; il modello di memoria non è più un buco di
  `docs/solutions/`.

## Sweep del 2026-09-09

Chiusa con evidenza: #26 (commento con `file:riga` su `main`). Aperte, condizione testata
falsa contro l'albero: #68, #64, #54, #48, #46, #33, #19, #17. #65 `blocking` per la GUI
del daemon, non toccata. #74 è stato mergiato dopo lo sweep e il suo diff non tocca nessuno
dei file che quelle otto condizioni testano.

## Stato machine-local, fragile

- **ESP-IDF c'è ed è v6.0.2**, installato con eim in `~/.espressif/v6.0.2/`. Si attiva con
  `source ~/.espressif/tools/activate_idf_v6.0.2.sh`. L'`export.sh` dentro esp-idf **non
  funziona** con questo layout: cerca un venv che eim non crea. Non è in nessun rc di zsh.
- **Worktree `.claude/worktrees/station-daemon-startup-03165a`**, ramo
  `claude/station-daemon-startup-03165a` a `2ac9689`, con tre file non committati:
  `CONCEPTS.md`, `parameters.yaml` modificati e
  `docs/plans/2026-09-08-2158-feat-station-daemon-plan.md` nuovo. È il piano del daemon di
  stazione dell'8 settembre e il primo posto da guardare per #64. Non sopravvive a una
  pulizia distratta: lo sweep dei worktree di oggi l'ha lasciato stare apposta.
- Catture in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/`, non committate
  (sessione 12 = keying e `TX_INFO`; sessione 10 = sysop). Sorgente DL4YHF in
  `~/Downloads/Remote_CW_Keyer_Sources.zip`; file CRLF, `grep -a`.
- Due debiti leggeri da #70 e #71 sotto **Da sistemare** in `.claude/code-quality.md`
  (righe 13-14): il ri-sorpasso di `consumer_resync()`, il `continue` che salta il delay
  del tick nel re-read della config. Il handoff dell'8 settembre li dava per registrati e
  non lo erano.
- 36 rami remoti mergiati di sessioni precedenti sono ancora su `origin`, in attesa di
  una parola del maintainer. Quelli locali e i sei worktree stantii sono stati rimossi
  oggi.
- Il blocco treecode di `components/keyer_led/CLAUDE.md` è rigenerato col cartografo;
  restano stantii quelli di `test_host` e `keyer_cwnet`, già in `code-quality.md`.

## Pattern e lessico

Review avversariale su Opus prima della PR, sonnet per il resto; il modello va sempre
dichiarato. Il maintainer vuole **manipolare / manipolazione**. Le domande di scelta
passano dalla chat, non dal tool di domanda bloccante: nell'app desktop il testo prima del
tool non arriva.
