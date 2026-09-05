---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-05T19:45:00Z"
title: "Sessione zero parziale: cinque ipotesi CWNet chiuse contro il riferimento vero"
summary: "Prima cattura reale del client DL4YHF: H1-H5 confermate (H3 e H5 con correzioni), H6 smentita, strumenti del banco in repo, dieci issue aperte, workflow del debito ridefinito."
keywords: ["cwnet", "dl4yhf", "sessione-zero", "oracolo-differenziale", "banco-di-prova", "issue-workflow"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32"
resume_focus: "Il loop di determinismo non ha più banco: H6 è smentita. Il primo lavoro sbloccato è #14, il server echo minimo. In alternativa #10 (TX su MORSE 0x10), che è la conformità vera."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "cwnet-bench-session-zero"
head: "a7aef65"
---

# Sessione zero parziale — CWNet contro il riferimento vero

Quattro commit su `cwnet-bench-session-zero`, PR aperta. Working tree pulito.

## Cosa è successo

L'handoff precedente diceva di ripartire dal peso probatorio delle fixture (KTD5). Ci abbiamo provato con `ce-brainstorm`, ma l'utente ha **riorientato la sessione** dopo poche battute: *«Lascia stare questo. Dobbiamo pensare come testare contro il golden standard DL4YHF»*. Il brainstorming su KTD5 è quindi **abbandonato, non concluso** — la domanda sulla durezza del meccanismo resta aperta e non è stata decisa.

Da lì la sessione è diventata una sessione zero parziale, ed è andata molto oltre le previsioni del piano.

## Il metodo che ha funzionato

Documentato per intero in [docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md](../../../../docs/solutions/architecture-patterns/reference-source-as-differential-oracle.md) — da leggere prima di toccare qualunque cosa di CWNet. In sintesi: `CwStreamEnc.c` di DL4YHF è C puro, compila con uno shim di quattro typedef, e il suo dominio di ingresso è abbastanza piccolo da fare un confronto **esaustivo** contro il nostro codec. Zero scarti. `CwNet.c` invece non compila (l'archivio pubblicato manca di `StringLib.h`, `yhf_type.h`, `QFile.h`, `YHF_*.h`, e l'autore non li dà: «sono lavori vecchi») — ma non serve, perché i byte li legge il nostro parser.

**Il permesso di DL4YHF c'è**, per email, e l'utente si è irritato quando l'ho rimesso in discussione dopo che me l'aveva già detto. Non riaprire l'argomento.

Il loopback della VM non era catturabile con Npcap, quindi il traffico è stato fatto passare da un **tap TCP** sul Mac. Gli strumenti sono in [tools/cwnet/](../../../../tools/cwnet/) con il README che spiega il fetch del codec di riferimento — che **non** è vendorizzato, per scelta esplicita dell'utente.

## Esiti (tabella completa nel piano, sezione Dependencies)

| | |
|---|---|
| H1, H2, H4 | confermate |
| H3 | confermata **ma corretta**: fine over a **14 dot-time**, non «10 dot o 500 ms». Il commento nel sorgente è sbagliato; codice e byte concordano |
| H5 | confermata, **con un difetto**: il peak-hold usa divisione intera, quindi si pianta a `istantaneo + <10 ms` e non scende più. Osservato nel log GUI dell'autore (`pk = 7 ms` fermo per 90 s con istantaneo 1–5) |
| H6 | **smentita** — vedi sotto |
| H7 | non osservabile senza la nostra scatola sul filo |

Non previste: il **PTT viaggia come stringa rigctld** (`set_ptt 1/0`) in frame `0x06`; il client **impacchetta** più eventi per frame quando ne trova in coda; i permessi sono una bitmask.

## H6 è il fatto che cambia i piani

Il server **non rimanda indietro il keying**, e non è una questione di configurazione o di latenza (l'utente ha ipotizzato la latenza; il sorgente dice di no). `CWNET_CMD_MORSE` viene emesso da un solo punto di `CwNet.c`, e la FIFO che lo alimenta si riempie solo sotto `iFunctionality == CWNET_FUNC_CLIENT`: **un'istanza server non emette mai MORSE**. Collegare un secondo client non serve.

Conseguenza: R7 perde il banco che il piano assumeva. Il ripiego era già previsto — un server echo nostro — ed è ora la issue #14. Regge, perché il confronto di **determinismo** non ha bisogno del riferimento ai capi; quello di **formato** sì, e quell'evidenza adesso ce l'abbiamo.

## Stato del lavoro

**Fatto e verificato.** Strumenti in `tools/cwnet/` (i tre programmi C compilano puliti, `difftest` con UBSan, `cwnet_dump` con `-Werror`; il confronto esaustivo dà 0 scarti). Esiti H1–H7 nel piano. Il learning di metodo. `CONCEPTS.md` creato da zero, 7 voci sul vocabolario CWNet. CLAUDE.md punta a `docs/solutions/` e `CONCEPTS.md`.

**Non fatto.** La suite host non è stata rilanciata in questa sessione — nessun codice di produzione è stato toccato, quindi non ci si aspettano regressioni, ma non è verificato. Il firmware non è compilabile su questa macchina (nessun ESP-IDF installato).

**Machine-local, non nel repo.** Le catture reali stanno in `/private/tmp/claude-501/-Users-sf-Developer-RemoteCWKeyer-esp32/bed2bd1e-*/scratchpad/oracle/*.bin` insieme al sorgente DL4YHF scaricato. **Sono già evaporate una volta** con un cambio di macchina in questa stessa sessione. Non sono committate perché #8 lo vieta. Se servono, si rifanno: il tap e la procedura sono in `tools/cwnet/README.md`.

## Workflow cambiato in corsa

L'utente ha introdotto una regola e poi l'ha **corretta due volte**, quindi conta la versione finale, in CLAUDE.md sotto *Work That Needs Deciding Becomes an Issue*: issue solo per lavoro che richiede una decisione, è schedulabile, non è validabile qui, o il cui costo si accumula. Tutto il resto si sistema sul posto — filare issue leggere è **QRM**. Debito leggero non sistemabile subito: una riga in `.claude/code-quality.md`.

Applicata a ritroso: #18 e #20 chiuse come QRM e corrette direttamente (path IDF, README).

Il README è stato riscritto **per un lettore esterno**, su indicazione dell'utente: niente banco di prova, niente istruzioni di build o di suite, DL4YHF citato e linkato.

## Dieci issue aperte

`#10` TX su MORSE 0x10 · `#11` RX multi-evento · `#12` filtro peak-hold (con la decisione se replicare il bug) · `#13` PTT rigctld · `#14` server echo · `#15` dissector U1 · `#16` devcontainer IDF v5.5.1 · `#17` stub USB · `#19` doc-review del piano.

Restano bloccanti `#7` (sequenza nota, cosa significa «identico», tolleranza) e `#8` (provenienza delle catture). **Bloccano ancora U5 e U6 e il commit delle fixture.** Nulla di quanto sopra le aggira.

## Come continuare

Il lavoro sbloccato più prossimo è **#14, il server echo**: senza, il determinismo non ha banco, ed è il pezzo che il resto del banco presuppone.

In alternativa, **#10 (TX su MORSE 0x10)**: è la conformità vera, quella per cui esiste il progetto, ed è ora dimostrabile invece che supposta. Non è in conflitto con #14 — sono due strade, non due opzioni esclusive; #14 serve a *misurare*, #10 a *essere corretti*.

Due cose in sospeso che non sono issue:

- **Il brainstorming su KTD5 non è stato concluso.** Se lo si riprende, la domanda aperta è quanto in là portare la distinzione fra fixture `reference/`, `ours/` e `synthetic/` — se una sintetica usata come atteso debba rompere la build, e se valga anche per il confronto di determinismo dove entrambi i capi vengono da `ours/`.
- **H5 porta con sé una decisione**, registrata in #12: replicare il difetto della divisione intera per massimizzare l'accordo col riferimento, o correggerlo e produrre un numero migliore che non corrisponderà.

Skill utili: `ce-work` per le issue sbloccate, `ce-brainstorm` se si riprende KTD5, `ce-doc-review` per #19.
