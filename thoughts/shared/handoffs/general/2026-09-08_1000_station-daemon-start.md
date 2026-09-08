---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T09:58:00Z"
title: "Il daemon di stazione: strategia decisa, #60 chiusa, #64 e #65 aperte, zero righe scritte"
summary: "Sessione 8 settembre 2026: STRATEGY.md rovesciato sul lato stazione (server nostro in C, daemon su PC Linux/Mac, DL4YHF riferimento del filo e non del prodotto), Decision #60 scritta e chiusa, Work #64 per il daemon, Decision #65 blocking per la GUI."
keywords: ["cwnet", "daemon", "server", "station", "dl4yhf", "gui", "issue-60", "issue-64", "issue-65", "strategy", "keyer_sim", "cwnet_echo"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
resume_focus: "Iniziare il daemon di stazione in C (#64) dal codec di keyer_cwnet compilato per host e dall'echo server del banco; la GUI aspetta la Decision #65, che è blocking solo per la GUI."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "c8ec541"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
---

# Il daemon di stazione: deciso, non iniziato

Ripresa da `2026-09-08_0912_cwnet-client-track.md`. Questa sessione non ha scritto codice:
ha cambiato la strategia, chiuso la Decision che la bloccava e aperto le due issue da cui
il lavoro parte. Tutto è su `main` a `c8ec541` (PR #63, mergiata dal maintainer).

## Cosa ha deciso il maintainer, e dove è scritto

Le decisioni sono del maintainer, prese in chat l'8 settembre; la prosa è mia.

- **Il programma DL4YHF è riferimento del filo, non del prodotto.** Scopo diverso dal nostro,
  Borland Windows only, GUI senza sorgente, CI-V al seguito, scelte di stazione opinabili
  (PTT tenuto 500 ms). `STRATEGY.md`, Purpose e Positioning.
- **Il prodotto è la coppia scatola + server nostro.** Le policy di stazione (PTT, cessione
  della chiave, buffer di riproduzione) sono nostre, prese una volta sola perché possediamo
  i due capi. La scatola da sola non è un prodotto. `STRATEGY.md`, Positioning.
- **Il server è un daemon su PC di stazione, Linux o Mac, in C, in questo repo, sul codec di
  `keyer_cwnet` compilato per host.** Opzione 3 di #60, contro la raccomandazione 2 (server
  sulla scatola). Motivo: le policy del riferimento non devono entrare nel path RT della
  scatola; il canale remoto in `sample.h` e il playback su Core 0 non servono più.
  `STRATEGY.md`, track "CWNet, i due capi"; #60, sezione Decision.
- **Audio, CI-V e spettro dentro CWNet: nessun impegno.** Il daemon implementa i comandi che
  la manipolazione richiede; il resto della stazione passa da dove passa oggi (Thetis).
  `STRATEGY.md`, Boundaries.
- **Niente scatola come server, per ora.** Una scatola con doppia personalità client/server è
  un repurpose utile anche per i test, parcheggiato come boundary. `STRATEGY.md`, Boundaries.
- **Repo unico, per ora.** Mia raccomandazione, accettata: ciò che i due capi condividono è la
  conoscenza del filo, e sta qui (fixture, catture, test). Lo split in un repo suo arriva
  quando il daemon avrà una cadenza di rilascio propria, come è successo al keyer.
- **Una piccola GUI col daemon.** Richiesta del maintainer, tecnologia non scelta: è #65.

## Tracker

- **#60** chiusa, `blocking` resta sull'issue come su ogni Decision decisa. Il corpo porta la
  decisione e un "What it blocks" riscritto per l'opzione 3. Il corpo era già oltre le 300
  parole prima di questa sessione; non l'ho tagliato.
- **#64** Work, aperta da me: il daemon. Condizione di chiusura: la suite host fa girare il
  daemon contro la cattura del client DL4YHF (CONNECT, PING, l'over della sessione 12 in
  `test_host/cwnet_fixtures.h`) e ne pinna i byte; il client della scatola completa un over
  contro di lui nel loop del banco con `tools/cwnet/keyer_sim.c` come stimolo. Blocked on:
  nothing.
- **#65** Decision `blocking`, aperta da me: quale GUI e in quale tecnologia. Quattro opzioni
  nel corpo (pagina su localhost, TUI, finestra nativa GTK4/nuklear/raygui, nessuna GUI);
  la mia raccomandazione è la TUI con lo stato anche su stdout. **Blocca solo la GUI**, non il
  core del daemon: A Blocking Issue Blocks vale per la GUI e per niente altro.
- Sweep fatto prima di questo handoff, dopo l'ultimo cambio al tree: nessuna condizione vera,
  nulla chiuso. Le nove issue aperte prima di #64 e #65 sono invariate (#57, #54, #48, #46,
  #33, #26, #25, #19, #17).

## Da dove parte il daemon: cosa esiste

- **Codec e protocollo, già compilati per host.** `test_host/CMakeLists.txt:89-93` compila
  `cwnet_timestamp.c`, `cwnet_frame.c`, `cwnet_ping.c`, `cwnet_client.c`, `cwnet_feed.c`
  senza ESP-IDF: è la prova che il codec regge un build host. Il daemon riusa quei file, non
  li copia. `cwstream_encode_timestamp()` è l'unica funzione verificata esaustivamente contro
  il riferimento (`tools/cwnet/diff_main.c`, #33).
- **Il seme del daemon**, `tools/cwnet/cwnet_echo.py`: server minimo in Python con CONNECT
  echo, PING, `TX_INFO`, `RPRT 0`, eco dei MORSE. Dice quali comandi bastano al client della
  scatola per credere di essere connesso. Va riletto per la sequenza, non portato riga per riga.
- **Lo stimolo**, `tools/cwnet/keyer_sim.c`: KeyerThread simulato sull'encoder DL4YHF, produce
  gli attesi sintetici. Compilazione in `tools/cwnet/README.md:57`.
- **Le fixture**, `test_host/cwnet_fixtures.h`: `ref_connect_echo`, `ref_first_over` (41 byte,
  PTT compreso), `ref_two_event_frames`, `synth_morse_frame`. Il primo test del daemon le
  legge dall'altro capo: riceve quello che il client manda.
- **Il PING** dal lato server: `.claude/code-quality.md:19-21` spiega i tre slot dei timestamp e
  chi legge cosa. Il server è l'iniziatore; la differenza si calcola sempre sul suo orologio.

## Le policy del riferimento, da decidere e non da copiare

Lette nel sorgente DL4YHF e registrate nel corpo di #60 con i `file:line`: riproduzione
ritardata di `iLatency_ms` = max(picco misurato, 250 configurati, 50); PTT tenuto 500 ms dopo
l'ultimo key-up riprodotto, sempre; lo stesso `iTxHangTime_ms` fa da watchdog che forza il
tasto su a FIFO vuota; chiave presa dal primo byte MORSE e rilasciata a timer 1 s che non
riparte alla presa remota, da cui il flap di `TX_INFO` fra nome e "nobody" durante l'over
(sessione 12, 23/24 annunci); `set_ptt` applicato all'arrivo, non allineato alla riproduzione.
Il client della scatola manda `set_ptt 1` dopo il key-down e `set_ptt 0` dopo `ptt_tail_ms`
(#13): il daemon è il primo posto dove quel PTT viene letto per davvero.

La regola dello STRATEGY: una modifica che riproduce una scelta di stazione del programma
DL4YHF **solo perché il riferimento la fa** è da respingere. Ogni policy del daemon nasce
da una scelta nostra, scritta nel corpo di #64 o in una Decision se non è ovvia.

## Sorgente e catture, machine-local

- Sorgente DL4YHF: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9…`), estratto
  in `tools/cwnet/ref/` solo nel checkout principale, gitignored. Recipe di fetch in
  `tools/cwnet/README.md:35-39`. File CRLF: `grep -a`. Permesso scritto ottenuto, non si
  ridiscute; l'archivio è incompleto e `CwNet.c` non compila da solo.
- Catture: `tmp/oracle/` nel checkout principale, non committate. Quali diventano fixture
  è #33.
- Il worktree `busy-chaum-c78815` è pulito e il suo branch è mergiato: si può buttare.

## Cosa non è stato fatto, di proposito

- Nessuna riga del daemon, nessun CMake per il target host, nessuna scelta di directory
  (`tools/cwnet/` come il banco, o una directory nuova: da decidere alla prima PR, non è una
  Decision).
- Nessuna scelta sul come il daemon manipola il rig dal PC (seriale, USB, GPIO di un
  adattatore): #64 dice "key output on the host" e non di più. Serve prima di chiudere #64;
  è una Decision se ha più di una risposta ragionevole.
- Il piano del banco (`docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md`) resta
  `requirements-only` per #19; le sue unità U5/U6 sull'echo server sono state superate dai
  fatti (l'echo esiste, #14) e ora dal daemon.

## Cosa ha funzionato in questa sessione

Il maintainer non vede il testo che precede una domanda bloccante nell'app desktop: la
bozza va messa nel messaggio finale, e la conferma arriva in chat. Prima di ogni domanda
di scelta: fase, decisione, conseguenze delle opzioni.
