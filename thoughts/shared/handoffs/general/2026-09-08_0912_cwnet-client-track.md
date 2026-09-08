---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T09:12:54Z"
title: "Il track CWNet client contro il riferimento: sei issue chiuse, il PTT capito, il server nostro in Decision"
summary: "Sessione 7-8 settembre 2026: TX MORSE con il cronometro del riferimento, feed dallo stream, RX MORSE, peak-hold, echo server, PTT come mirror del PTT locale; letto nel sorgente cosa fa il server DL4YHF e aperta #60."
keywords: ["cwnet", "dl4yhf", "morse", "set_ptt", "echo-server", "issue-25", "issue-57", "issue-60", "feed", "stream-time"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/handoff-non-affrontato-d8e0f0"
resume_focus: "#25 con la scelta «manipola comunque e la scatola lo segnala»; il comando console che svuota la FIFO RX per chiudere il loop di determinismo sul link; la metà libera di #57. Server nostro solo dopo la Decision #60."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "cd11e08"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/handoff-non-affrontato-d8e0f0"
---

# Il track CWNet client contro il riferimento

Ripresa da `2026-09-07_1821_keyer-split-esp32keyertest.md`: il maintainer ha scelto di lavorare
qui sul repo principale, in parallelo al repo keyer. Sei PR mergiate, tutte con CI verde
(host tests nelle due varianti e `firmware-build`): **#56** (#10), **#58** (#55), **#59** (#11,
#12), **#61** (#14), **#62** (#13). Suite host da 176 a 205 test. Niente provato su hardware:
non c'e' ESP-IDF su questa macchina.

## Cosa esiste adesso, e dove

- **TX MORSE** in `components/keyer_cwnet/src/cwnet_client.c`: `send_morse()`,
  `cwnet_client_send_key_event(client, key_down, at_ms)`, `cwnet_client_poll(client, now_ms, dot_ms)`.
  Regola del client DL4YHF letta in `KeyerThread.c:2707-2762`: primo edge di un over attesa 0,
  cronometro che avanza dei ms **codificati**, split oltre 1165 ms, secondo key-up dopo 14 dot.
  Frame fino a 128 byte con ri-base sull'edge oltre. `abort_over()` chiude il filo su overrun o send fallita.
- **Feed dallo stream** in `cwnet_feed.c`: consumer best-effort proprio di CWNet, tempo di stream
  contando i tick (1 per campione, N per marker di silenzio), fine over su tempo invecchiato con
  l'orologio campionato subito prima del drain, 64 edge per pass. Il socket layer butta la
  sessione su `send()` parziale. `bg_task` non inoltra piu' nulla.
- **RX MORSE**: FIFO da 128 a byte grezzi, `cwnet_client_rx_pop()`, `rx_buffered_ms()`,
  `rx_has_end_of_over()` (due key-up consecutivi). Nessuno la riproduce: nel riferimento un client
  scarta i MORSE e il server non ne manda.
- **Latenza**: `latency_peak_ms` = peak-hold del riferimento, floor compreso (decisione nel corpo di #12).
- **PTT**: `set_ptt 1` dopo il byte MORSE del key-down, `set_ptt 0` dopo `timing.ptt_tail_ms`
  dal `poll`; `RPRT n` conservato in `rig_result`. Decisione nel corpo di #13.
- **Banco**: `tools/cwnet/cwnet_echo.py` (server minimo con eco MORSE, PING, TX_INFO, RPRT 0),
  `tools/cwnet/keyer_sim.c` (KeyerThread simulato sull'encoder DL4YHF: provenienza degli attesi sintetici).
- **Fixture**: `test_host/cwnet_fixtures.h`: CONNECT echo, primo over della sessione 12 (41 byte,
  PTT compreso, ora pinnato intero da client e da stream), frame a due eventi, frame sintetico.

## Cosa dice il server DL4YHF, letto riga per riga

Tutto in `.claude/code-quality.md` e nei corpi di #60, #25 (commento), #13 (commento). In breve:
riproduzione ritardata di `iLatency_ms` = max(picco misurato, 250 configurati, 50);
PTT tenuto 500 ms dopo l'ultimo key-up riprodotto, sempre; lo stesso `iTxHangTime_ms` e' il
watchdog che forza il tasto su a FIFO vuota; chiave presa dal primo byte MORSE, rilasciata a
timer 1 s che non riparte alla presa remota, quindi `TX_INFO` oscilla tra il nome e «nobody»
durante l'over (23/24 annunci in cattura); `set_ptt` applicato all'arrivo, da un client e'
il mirror del suo PTT locale. Il maintainer ha concluso che il server va fatto nostro: **#60**,
Decision `blocking`, blocca solo il ruolo server.

## Aperte, e cosa manca a ciascuna

- **#25**: modello libera/mia/altrui da `TX_INFO` (mia = indice >= 1 e nome uguale al nostro
  username), «nobody» che conta come libera solo se regge oltre il flap; scelta consigliata e
  accettata a voce: manipola comunque e la scatola lo segnala. Non ancora scritta nel corpo.
- **#57**: difetto del core, `write_idx` pubblicato prima dello slot e `stream_read` a
  `lag == capacity`; la meta' `>= capacity` e' libera, l'ordine di pubblicazione aspetta il maintainer.
- **Loop di determinismo sul link**: manca un modo di leggere la FIFO RX dalla scatola
  (comando console `cwnet rx`, righe `key wait`). Sull'host e' gia' chiuso
  (`test_client_round_trip_returns_the_edges_sent`).
- #26, #33, #46, #48, #54, #17, #19: invariate, con le decisioni gia' annotate nei corpi.
- Sweep fatto prima di questo handoff: nessuna condizione vera nel tree, nulla chiuso.

## Pattern che ha funzionato

Review avversariale su `opus` con mandato di falsificare, prima della PR: su #55 ha trovato
quattro buchi veri (send fallita ignorata, troncamento a 8 byte, orologio in anticipo, resync
silenzioso) e il difetto del core. Ripetere su ogni pezzo che gira sulla scatola senza prova.

## Lessico

Il maintainer vuole **manipolare / manipolazione**, non calchi dall'inglese; e non vuole
sentire nominare il demone di Hamlib: il blocco 0x06 e' «la stringa di controllo radio».

## Stato machine-local, fragile

- Sorgente DL4YHF: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9...`), estratto
  nello scratchpad di sessione (evapora) e in `tools/cwnet/ref/` solo nel checkout principale
  (gitignored). File CRLF: `grep -a`, `tr -d '\r'`.
- Catture in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/` (sessione 12 = keying).
- Debito leggero in `.claude/code-quality.md`: blocchi treecode stantii (`test_host`, `keyer_core`,
  `keyer_cwnet`), clamp del marker di silenzio a 65,5 s.
