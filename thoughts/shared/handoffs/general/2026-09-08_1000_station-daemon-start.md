---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T09:58:00Z"
title: "Daemon di stazione e client host: strategia decisa, #60 chiusa, #64, #65 e #68 aperte, zero righe scritte"
summary: "Sessione 8 settembre 2026: STRATEGY.md rovesciato sul lato stazione (server nostro in C, daemon su PC Linux/Mac, DL4YHF riferimento del filo e non del prodotto) e poi sul client (programma host portabile, Windows primo target, Mac best effort); #60 chiusa, Work #64 daemon, Decision #65 blocking per la GUI, Work #68 client host."
keywords: ["cwnet", "daemon", "server", "station", "client", "host", "windows", "serial", "dl4yhf", "gui", "issue-60", "issue-64", "issue-65", "issue-68", "strategy", "keyer_sim", "cwnet_echo"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
resume_focus: "Due partenze indipendenti: il daemon di stazione in C (#64) dal codec di keyer_cwnet compilato per host e dall'echo server del banco; il client host portabile (#68) da cwnet_socket, l'unico file di piattaforma. La GUI aspetta la Decision #65, blocking solo per la GUI."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "c9b0ea1"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
---

# Daemon di stazione e client host: decisi, non iniziati

Ripresa da `2026-09-08_0912_cwnet-client-track.md`. Questa sessione non ha scritto codice:
ha cambiato la strategia due volte, chiuso la Decision che la bloccava e aperto le tre
issue da cui il lavoro parte. Tutto è su `main`: PR #63 (lato stazione) e PR #67 (client
host), entrambe mergiate dal maintainer.

## Cosa ha deciso il maintainer, e dove è scritto

Le decisioni sono del maintainer, prese in chat l'8 settembre; la prosa è mia.

- **Il programma DL4YHF è riferimento del filo, non del prodotto.** Scopo diverso dal nostro,
  Borland Windows only, GUI senza sorgente, CI-V al seguito, scelte di stazione opinabili
  (PTT tenuto 500 ms). `STRATEGY.md`, Purpose e Positioning.
- **Il prodotto sono i due capi nostri: il server, e un client che è la scatola o un
  programma su PC.** Le policy di stazione (PTT, cessione della chiave, buffer di
  riproduzione) sono nostre, prese una volta sola perché possediamo i due capi. La scatola
  da sola non è un prodotto. `STRATEGY.md`, Positioning (PR #67 per la parte client).
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
- **Il client è anche un programma host portabile, Windows primo target, Linux dopo, Mac best
  effort.** Bisogno emerso da un progetto parallelo del maintainer; il paddle entra dalle
  linee di controllo di una seriale USB. Windows only fu scartato perché il client host è
  anche il banco senza scatola, e il banco gira sul Mac dove si sviluppa. Mac best effort e
  non escluso: macOS non ha `TIOCMIWAIT` e si polla `TIOCMGET`, ma il polling a 1 ms è
  equivalente agli eventi di Windows/Linux, che nascono comunque dai pacchetti USB al
  latency timer dell'adattatore; il limite vero è che il driver Apple lascia il latency
  timer FTDI a 16 ms, un dot a 40 WPM. `STRATEGY.md`, Users, Boundaries, track "CWNet, i
  due capi". La riga di Boundaries "Feeling del K8 sul client PC: senza metrica" è una mia
  proposta lasciata dentro dal maintainer.
- **Chi usa il client Windows** non è stato detto: Users lo chiama "l'OM che manipola da un
  PC Windows". Se il progetto parallelo ha una persona o una situazione precisa, va lì.

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
- **#68** Work, aperta da me: il client host. Condizione di chiusura: programma costruito dal
  CMake del repo su Windows (MinGW o MSVC) e Linux, paddle da due linee di controllo di una
  seriale al tick da 1 ms della FSM, FSM del submodule nello stream, `keyer_cwnet` sopra un
  socket layer winsock/POSIX con orologio monotono; la suite host ne pinna il filo con lo
  stesso test del client della scatola, un job CI Windows lo compila, e il jitter del tick su
  Windows è misurato e scritto prima di rivendicare la metrica K8. Blocked on: nothing.
  Client e daemon (#64) sono i due capi del loop del banco senza scatola: partono
  indipendenti e si incontrano lì.
- **#25** chiusa da un'altra sessione in parallelo (PR #66): `cwnet_client_get_key_holder()`
  e il callsign pinnato sul filo. Non l'ho riletta; il corpo di #25 e la PR dicono cosa c'è.
- Sweep fatto prima della prima versione di questo handoff, dopo il merge di #63: nessuna
  condizione vera, nulla chiuso. Da allora #25 è stata chiusa da un'altra sessione; #57,
  #54, #48, #46, #33, #26, #19, #17 non le ho riverificate dopo PR #66.

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

## Da dove parte il client host: cosa esiste

- Lo stesso core del client della scatola, già compilato per host in `test_host` (sopra).
  `cwnet_socket.c` è l'unico file di `keyer_cwnet` legato a ESP-IDF: socket BSD più
  `esp_timer` e FreeRTOS (`components/keyer_cwnet/src/cwnet_socket.c:13-19`). Il client host
  ha bisogno di una variante winsock/POSIX di quel file e di un orologio monotono, non di
  altro dal lato protocollo.
- La FSM iambic è nel submodule `keyer_iambic` (Esp32KeyerTest), C puro con interfaccia
  congelata in `iambic.h` e `sample.h`; il keying stream in `keyer_core` è C puro.
- Manca tutto il resto: l'ingresso del paddle da CTS/DSR (Windows `WaitCommEvent` con
  `EV_CTS|EV_DSR`, Linux `TIOCMIWAIT`, Mac polling di `TIOCMGET`), il tick da 1 ms su Windows
  (`timeBeginPeriod(1)` e thread ad alta priorità, jitter da misurare), il sidetone su PC, il
  CMake per il target e il runner Windows in CI. Nessuno di questi è stato scelto o scritto.

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

- Nessuna riga del daemon né del client host, nessun CMake per i target host, nessuna scelta
  di directory (`tools/cwnet/` come il banco, o una directory nuova: da decidere alla prima
  PR, non è una Decision). Daemon e client host condividono il socket layer e l'orologio:
  chi parte per primo li scrive per entrambi.
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
