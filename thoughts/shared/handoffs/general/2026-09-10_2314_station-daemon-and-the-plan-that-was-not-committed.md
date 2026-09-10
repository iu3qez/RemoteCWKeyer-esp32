---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-10T23:14:00Z"
title: "Il daemon di stazione, e il piano che non era committato"
summary: "Sessione 10 settembre 2026: le otto unità del daemon di stazione mergiate in #77, poi scoperto dopo il merge che il motore manipolava spazzatura con un operatore dal vivo; #78 corregge quello e le altre quattro voci di una revisione del piano che viveva solo in un file non committato. Restano il passo di banco (#64), Linux (#76) e una decisione aperta sulla rete di inattività."
keywords: ["station-daemon", "cwnetd", "cwnet_play", "cwnet_server", "cwnet_rxfifo", "underrun", "grace", "buffer-floor", "edges-descriptor", "issue-64", "issue-65", "issue-68", "issue-76", "pr-77", "pr-78", "plan-provenance", "live-keying"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/colour-rule-key-verdict-led-da2126"
resume_focus: "La PR #78 attende il merge del maintainer. Dopo: la decisione aperta sulla rete di inattività (R6), il passo di banco con la scatola che tiene aperta la #64, e il client host della #68, che riusa host/platform/ e cwnet_rxfifo.h."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "fix/64-the-element-holds"
head: "fed9073"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/colour-rule-key-verdict-led-da2126"
---

# Il daemon di stazione, e il piano che non era committato

Ripresa da `2026-09-09_2054_colour-rule-key-verdict-led-pruning.md`, che indicava il lato
stazione come prossimo passo. Il lato stazione adesso esiste: [#77](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/77)
mergiata in `606d39e`, [#78](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/78) aperta
e verde, in attesa del maintainer.

Il fatto centrale della sessione non è il codice: è che **una revisione del piano è
esistita per venti ore in un solo file non committato**, e nel frattempo la #77 ha
implementato il testo vecchio. Il difetto che ne è uscito era grave. Chi legge questo
handoff dovrebbe leggere prima la sezione «La provenienza del piano».

## Cosa esiste adesso, e dove

- **La stazione manipola.** `host/cwnetd/` è il daemon: un thread, `sock_poll()` con
  timeout alla prossima scadenza, mai un tick fisso. `host/platform/` è il layer POSIX,
  con la cucitura winsock nei nomi perché il client host della #68 lo riusi.
- **Il core è puro e host-only.** `components/keyer_cwnet/cwnet_server.[ch]` porta un
  client dal CONNECT a READY, arbitra la chiave e annuncia il titolare;
  `cwnet_play.[ch]` trasforma i byte MORSE in fronti. Nessuno dei due legge l'orologio o
  stampa: il tempo è un parametro, gli eventi tornano in una struttura. Sono
  deliberatamente fuori dalle SRCS del componente ESP-IDF, e sono elencati a mano in
  `host/CMakeLists.txt` e `test_host/CMakeLists.txt`.
- **Una FIFO sola per i due capi**: `components/keyer_cwnet/include/cwnet_rxfifo.h`,
  header-only. I timestamp restano separati apposta, e il perché è nell'header: la
  scatola timbra col contatore a 31 bit del riferimento, che wrappa, il daemon con un
  istante monotono a 64 bit.
- **Le prove del filo** sono in `test_host/test_cwnet_server.c` e `test_cwnet_play.c`,
  pinnate su `test_host/cwnet_fixtures.h`, cioè la cattura del 2026-09-05.
- **La procedura di banco e i numeri** stanno in `host/cwnetd/README.md`. Il tempo di
  scambio dopo la TX è 200 ms misurati, non calcolati.

## La provenienza del piano, che è la cosa da non ripetere

Il piano `docs/plans/2026-09-08-2158-feat-station-daemon-plan.md` esisteva in due
versioni. Quella che la #77 ha eseguito era la copia su disco alle 23:01 del 9 settembre.
Una revisione più recente, scritta alle 01:30 del 10, viveva **solo** come file non
tracciato nel worktree `station-daemon-startup-03165a`: nessun commit, nessun ramo, e
`git log --all -S` non trovava una riga del suo testo.

Undici voci differivano, e non erano di forma: la regola dell'underrun, il pavimento del
buffer, il descrittore dei fronti, l'estrazione della FIFO. La #78 la committa **come
primo commit** (`37b8364`) proprio per chiudere quel buco: l'autorità deve stare in git
prima che il codice le risponda.

Come è stata scoperta: alla fine della sessione, controllando se quel worktree tenesse
ancora qualcosa di unico prima di proporne la pulizia. Se non l'avessi controllato, la
revisione sarebbe stata cancellata con il worktree.

**Sull'attribuzione, e questa è la mia lettura, non un fatto accertato.** Il maintainer
ha detto di aver fatto lui quelle modifiche. Un'altra sessione Claude, la `ce-plan` in
quel worktree, ha poi scritto di averle scritte lei fra le 21:00 e le 22:00 del 10,
dopo il merge della #77. Il filesystem dice che il file non è toccato dalle 01:30 del 10,
nove ore *prima* del merge, e il contenuto era già quello. Il contenuto tecnico di quella
sessione era invece verificato e corretto in due punti su due che ho controllato: la sua
riproduzione indipendente del difetto, e la citazione `CwNet.c:164` sui 250 ms. Non ho
risolto la contraddizione sull'autorship e non l'ho usata per decidere niente.

## Il difetto che il merge non ha fermato

Il motore trattava «FIFO vuota alla scadenza di un fronte» come underrun. Ma un operatore
manda un byte per ogni fronte **quando accade**, quindi durante un elemento più lungo del
buffer la FIFO è vuota per costruzione. Risultato con la scatola al tasto: la lettera A
usciva come un punto corretto da 48 ms e poi un lampo di durata zero al posto della linea.

I 303 test erano verdi perché ogni fixture viene consegnata in una raffica. Il test che
distingue le due regole è
`test_play_an_over_delivered_as_it_is_keyed_plays_like_a_buffered_one`: gli stessi byte
uno alla volta al proprio istante di arrivo.

Verifica eseguita, e vale più dei test: un client che manipola in tempo reale contro il
daemon vero. Prima del fix, il lampo di durata zero; dopo, punto 48, spazio 48, linea 144,
coda del PTT 100, nessun fault. Lo script è machine-local, nello scratchpad di sessione,
e non è stato committato: si riscrive in venti righe.

## Decisioni, e di chi sono

- **Del maintainer**: non aprire una issue per il difetto del parser, perché sarebbe QRM
  (quindi quella riga della Definition of done della #77 resta vuota per scelta, e la PR
  lo dice); aprire invece una issue per la misura su Linux, che è la #76; le undici voci
  della revisione del piano, secondo quanto ha dichiarato lui.
- **Mie**: il pavimento del buffer espresso nei test come costante e non come numero, così
  il prossimo cambio non passa inosservato; l'allineamento del nome dell'evento del motore
  a ciò che adesso significa; il rifiuto, in review, di estrarre una FIFO generica —
  **rovesciato dal piano**, ed è stato giusto rovesciarlo: da quando i due capi usano le
  stesse regole di fine over, due copie che divergono diventano due comportamenti diversi
  sullo stesso filo.
- **Di un worker, approvata da me**: fra «i fronti non si scartano mai» e «il loop non si
  ferma mai» vince il primo, perché quelle righe *sono* la misura del jitter; il costo è
  reso visibile su una riga di stato invece che nascosto.

## Cosa resta aperto

- **La decisione sulla rete di inattività (R6).** Misura il silenzio sul filo, non in
  riproduzione: un client che accoda più del timeout si vede togliere la chiave a metà
  trasmissione. La scatola non può farlo, un client che accoda sì. È una riga più un test,
  ma cambia il significato di R6, quindi è policy di stazione. Chiesta al maintainer due
  volte, senza risposta; sta scritta sotto «Not done here» nella #78.
- **[#64](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/64) resta aperta** sulla
  metà di banco. La procedura è in `host/cwnetd/README.md`, sezione «Banco con la
  scatola». Un commento sulla issue registra che la metà host regge.
- **[#76](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/76)**: il jitter è
  misurato solo su macOS. La CI compila e testa `host/` anche su Ubuntu, che non è la
  stessa cosa.
- **[#65](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/65) resta `blocking`** per
  la sola GUI del daemon, e nessuna delle due PR ne contiene una.
- **[#68](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/68)**, il client host, è il
  prossimo pezzo naturale: riusa `host/platform/` e `cwnet_rxfifo.h`, che sono stati
  scritti pensando a lui.

## Stato locale e trappole

- Il worktree `station-daemon-startup-03165a` non tiene più niente di unico: il piano è
  committato in `37b8364`, e i suoi `CONCEPTS.md` e `parameters.yaml` sono identici a
  quelli su `main`. Prima di questa sessione era l'unica copia della revisione.
- **Unity in questo repo non ha auto-discovery**: una funzione di test non dichiarata e
  non passata a `RUN_TEST` in `test_host/test_main.c` compila e non gira mai. È successo
  due volte in questa sessione. Quando più agenti lavorano in parallelo conviene che quel
  file lo cabli una persona sola.
- `SendMessage` è disabilitato in questa sessione: non si può rispondere alle altre
  sessioni Claude, solo passare per il maintainer.
- Il server MCP di GitHub non si connette (`Authorization header is badly formatted`); la
  CLI `gh` funziona ed è quella che ho usato.

## Verifica eseguita

Suite host 312/312, liscia e con ASan/UBSan. `ctest` di `host/` verde in entrambe le
varianti. CI verde su diciotto controlli per la #78, `firmware-build` compresa, che conta
perché `cwnet_client.c` è cambiato. Più la prova dal vivo descritta sopra.
