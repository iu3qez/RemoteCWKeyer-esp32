---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-05T20:15:00Z"
title: "Sessione zero parziale CWNet, e il workflow del debito riscritto due volte"
summary: "Cinque ipotesi CWNet chiuse contro il riferimento vero (H6 smentita), strumenti del banco in repo, dieci issue aperte, due regole di workflow nuove corrette da una review, e due PR di marzo da decidere."
keywords: ["cwnet", "dl4yhf", "sessione-zero", "oracolo-differenziale", "issue-workflow", "pr-stale"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32"
resume_focus: "Due cose aperte che aspettano l'utente: la decisione sulle PR #2 e #3 di marzo (analisi pronta, non chiuse), e poi il lavoro sbloccato — #14 server echo, o #10 TX su MORSE 0x10."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "cwnet-bench-session-zero"
head: "30fbd45"
---

# Sessione zero parziale, e il workflow riscritto

Sette commit su `cwnet-bench-session-zero`, [PR #21](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/21) aperta e pushata. Working tree pulito.

La sessione ha fatto due cose diverse: ha chiuso cinque ipotesi CWNet contro il riferimento vero, e ha cambiato il modo di tracciare il lavoro. La seconda parte è arrivata dopo, su richiesta dell'utente, ed è quella con più spigoli.

## Come è iniziata, e come è stata dirottata

L'handoff precedente diceva di ripartire dal peso probatorio delle fixture (KTD5). Ci abbiamo provato con `ce-brainstorm`, ma l'utente ha **riorientato dopo poche battute**: *«Lascia stare questo. Dobbiamo pensare come testare contro il golden standard DL4YHF»*. Quel brainstorming è **abbandonato, non concluso**: la domanda su quanto in là portare la distinzione fra fixture `reference/`, `ours/` e `synthetic/` resta aperta e non decisa.

## Parte 1 — la sessione zero

Metodo per intero in [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md), da leggere prima di toccare CWNet. In breve: `CwStreamEnc.c` di DL4YHF è C puro, compila con uno shim di quattro typedef, e il dominio di ingresso è piccolo abbastanza per un confronto **esaustivo** col nostro codec. Zero scarti. `CwNet.c` non compila (mancano `StringLib.h`, `yhf_type.h`, `QFile.h`, `YHF_*.h`; l'autore non li dà) ma non serve: i byte li legge il nostro parser.

**Il permesso di DL4YHF c'è, per email.** L'utente si è irritato quando l'ho rimesso in discussione dopo averlo già detto. Non riaprire.

Il loopback della VM non era catturabile con Npcap, quindi il traffico è passato da un **tap TCP** sul Mac. Strumenti in [tools/cwnet/](../../../../tools/cwnet/); il codec di riferimento **non** è vendorizzato, per scelta esplicita dell'utente, e il README ha il comando di fetch.

### Esiti (tabella completa nel piano, sezione Dependencies)

| | |
|---|---|
| H1, H2, H4 | confermate |
| H3 | confermata **ma corretta**: fine over a **14 dot-time**, non «10 dot o 500 ms». Il commento nel sorgente è sbagliato; codice e byte concordano |
| H5 | confermata **con un difetto**: il peak-hold usa divisione intera, si pianta a `istantaneo + <10 ms` e non scende più. Visto nel log GUI dell'autore (`pk = 7 ms` fermo 90 s con istantaneo 1–5) |
| H6 | **smentita** |
| H7 | non osservabile senza la nostra scatola sul filo |

Non previste: il **PTT viaggia come stringa rigctld** (`set_ptt 1/0`) in frame `0x06`; il client **impacchetta** più eventi per frame quando ne trova in coda; i permessi sono una bitmask.

### H6 è il fatto che cambia i piani

Il server **non rimanda indietro il keying**, e non è latenza (l'utente lo aveva ipotizzato; il sorgente dice di no). `CWNET_CMD_MORSE` esce da un solo punto di `CwNet.c`, e la FIFO che lo alimenta si riempie solo sotto `iFunctionality == CWNET_FUNC_CLIENT`: **un'istanza server non emette mai MORSE**. Un secondo client non serve.

R7 perde il banco che assumeva. Il ripiego era previsto — un server echo nostro — ed è la issue #14. Regge: il confronto di **determinismo** non ha bisogno del riferimento ai capi, quello di **formato** sì e quell'evidenza ora c'è.

## Parte 2 — il workflow, cambiato e poi corretto

L'utente ha chiesto che problemi e debiti diventino issue GitHub. La regola è stata scritta, **applicata subito**, e ha prodotto dieci issue in una passata — due delle quali QRM chiuse nella stessa ora, perché la prima stesura diceva «a problem, a debt, a stub» senza tracciare alcuna linea. L'utente ha corretto due volte: prima il criterio, poi il fatto che i debiti leggeri non vanno sul tracker («sarebbero solo QRM come diciamo noi radioamatori»).

Da lì è nata la seconda regola, su sua richiesta: **i cambi di workflow vanno in review**. Entrambe stanno in CLAUDE.md sotto Critical Constraints. Vale la **versione finale**, non le intermedie.

Poi la review (`/code-review`) sulle due regole ha trovato **undici difetti reali su dodici rilievi**, tutti corretti in `30fbd45`. I tre che contano:

- «Reviewed before it stands» **non aveva un meccanismo**: CLAUDE.md è in vigore nell'istante in cui lo scrivi, non esiste stato dormiente. Ora la finestra è la PR, il revisore è il maintainer, e un agente che revisiona la propria regola non conta.
- Le due regole **si contraddicevano** su un caso già in repo (il pin del devcontainer è insieme «wrong version» e «cannot be validated here»). Ora le quattro condizioni vincono sempre.
- La scappatoia del debito leggero **scavalcava la Definition of done**: un fix "ovvio" in `keyer_cwnet/` sarebbe passato senza host test. Ora è esplicito che non passa.

Un rilievo **respinto**: sosteneva che i solution doc non hanno `tags:`. Ce l'hanno entrambi, verificato.

**Non riconciliato di proposito**: i duplicati fra tracker e `.claude/feature-status.md` / `code-quality.md` (es. #16 e #17). Riconciliarli sarebbe applicare una regola fresca al backlog, che è ciò che la regola nuova vieta finché il maintainer non la approva. La regola dice chi vince (l'issue); l'esecuzione aspetta.

## Stato del lavoro

**Fatto e verificato.** Strumenti in `tools/cwnet/` (i tre programmi C compilano puliti, `difftest` con UBSan, `cwnet_dump` con `-Werror`; confronto esaustivo 0 scarti). Esiti H1–H7 nel piano. Il learning di metodo. `CONCEPTS.md` da zero, 7 voci. CLAUDE.md punta a `docs/solutions/`, `CONCEPTS.md` e `thoughts/shared/handoffs/`. README riscritto per un lettore esterno (diceva che il progetto è in Rust: 0 file `.rs`, 77 `.c`).

**Non fatto.** La suite host **non è stata rilanciata** in questa sessione. Nessun codice di produzione è stato toccato, quindi non ci si aspettano regressioni, ma non è verificato. Il firmware non è compilabile qui (nessun ESP-IDF installato; la riga di build in CLAUDE.md è stata corretta due volte e ora vuole `IDF_PATH` esplicito).

**Machine-local, non nel repo.** Le catture reali stanno in `/private/tmp/claude-501/-Users-sf-Developer-RemoteCWKeyer-esp32/bed2bd1e-*/scratchpad/oracle/*.bin` col sorgente DL4YHF scaricato. **Sono già evaporate una volta** per un cambio macchina in questa stessa sessione. Non committate: lo vieta #8. Se servono si rifanno, procedura in `tools/cwnet/README.md`.

## Issue

Aperte: `#10` TX su MORSE 0x10 · `#11` RX multi-evento · `#12` filtro peak-hold (con la decisione se replicare il bug) · `#13` PTT rigctld · `#14` server echo · `#15` dissector U1 · `#16` devcontainer IDF v5.5.1 · `#17` stub USB · `#19` doc-review del piano.

Chiuse come QRM e corrette sul posto: `#18` (path IDF) e `#20` (README).

Bloccanti, invariate: `#7` (sequenza nota, cosa significa «identico», tolleranza) e `#8` (provenienza delle catture). **Bloccano ancora U5, U6 e il commit delle fixture.** Niente di quanto sopra le aggira.

## In attesa di una decisione dell'utente: le due PR di marzo

Analizzate a fine sessione, **non chiuse** — sono PR sue e chiuderle è visibile. Entrambe partono da `3c827a1` (1 marzo); la migrazione a ESP-IDF v6 è `1b91bec` (7 maggio): **sono scritte contro v5 e non hanno mai visto v6**.

- **[#3](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/3) OTA via Web UI** — git dice `MERGEABLE`, ma **non compila**: il suo `CMakeLists.txt` ha `REQUIRES json`, componente rimosso in v6 e che CLAUDE.md vieta esplicitamente; e `api_firmware.c` include `esp_ota_ops.h`, `esp_app_desc.h`, `esp_partition.h`, `usb_uf2.h` senza REQUIRES corrispondenti. Fuori bound due volte: `STRATEGY.md` dice «Niente OTA ora» e «WebUI: nessun investimento».
- **[#2](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/2) iambic timeline markers** — `CONFLICTING`. Non è quello che dice il titolo: contiene i marker, **più** un captive portal DNS, **più** i documenti di design e piano della #3. Il pezzo centrale allarga `sample.flags` da `uint8_t` a `uint16_t` (sample da 6 a 7 byte): è una modifica al **layout dell'unica interfaccia dell'architettura**, sul path RT. I conflitti sono su `iambic.c`, dove la sessione precedente aveva corretto due bug.

**Da salvare, ed è nella #2**: i quattro flag `FLAG_MEM_WINDOW`, `FLAG_SQUEEZE`, `FLAG_MEM_ARMED`, `FLAG_MODE_B_BONUS` sono letteralmente la metrica K8 di `STRATEGY.md` («quale elemento parte, quando arma la memoria, cosa fa lo squeeze»). Fuori bound è la pagina Timeline della WebUI, non i flag.

**Proposta fatta all'utente, senza risposta**: chiuderle entrambe senza cancellare i branch, e aprire tre issue — strumentazione FSM per la track K8 (con la decisione sul sample a 7 byte), OTA congelata per boundary, captive portal DNS parcheggiato.

## Come continuare

Prima la decisione sulle due PR, che è ferma su di lui e ha l'analisi già pronta qui sopra.

Poi, sul lavoro sbloccato, due strade non esclusive: **#14 (server echo)** rende misurabile il determinismo, che senza H6 non ha banco; **#10 (TX su MORSE 0x10)** è la conformità vera, quella per cui esiste il progetto, ora dimostrabile invece che supposta.

Due code non tracciate come issue:

- **Il brainstorming su KTD5 non è concluso** — se una fixture sintetica usata come atteso debba rompere la build, e se valga anche per il confronto di determinismo dove entrambi i capi vengono da `ours/`.
- **H5 porta una decisione**, registrata in #12: replicare il difetto della divisione intera per massimizzare l'accordo col riferimento, o correggerlo e produrre un numero migliore che non corrisponderà.

Skill utili: `ce-work` per le issue sbloccate, `ce-brainstorm` se si riprende KTD5, `ce-doc-review` per #19.
