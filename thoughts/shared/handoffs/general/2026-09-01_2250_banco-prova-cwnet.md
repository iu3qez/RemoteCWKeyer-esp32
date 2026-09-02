---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-01T22:50:41Z"
title: "Ripresa del progetto: strategia, CI, e il banco di prova CWNet"
summary: "Sessione che ha ridato una strategia al progetto, reso verde la suite host trovando due bug veri, creato la CI da zero, e pianificato il banco di prova CWNet fino a due issue bloccanti."
keywords: ["cwnet", "banco-di-prova", "strategia", "ci", "dl4yhf", "issue-bloccanti"]
cwd: "/home/user/RemoteCWKeyer-esp32"
resume_focus: "Riprendere il peso probatorio delle fixture (KTD5) - l'utente lo ha marcato molto importante a fine sessione - poi #7 o U1-U4"
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "3525bf3ca777b861fb4448911a569e6d82b541af"
branch: "claude/remote-environment-setup-vdjx7v"
head: "123405eede29836d774755049016779582e313fe"
---

# Ripresa del progetto: strategia, CI, banco di prova CWNet

Otto commit su `claude/remote-environment-setup-vdjx7v`, tutti in [PR #6](https://github.com/iu3qez/RemoteCWKeyer-esp32/pull/6). CI verde su entrambe le varianti della matrice all'ultimo push.

## Perché questa sessione è esistita

L'utente ha aperto dicendo che il progetto era partito con delle idee, poi le idee erano sbiadite, il fondamento era solido ma pieno di incoerenze perché mancava una strategia. Proposta sua: rimettere a punto strategia, modo di lavorare, e CI.

La diagnosi che ne è uscita, e che regge tutto il resto: il progetto si è arenato **due volte** per lo stesso motivo, e non è che mancasse la voglia. Mancava un riferimento contro cui dimostrare le cose. Il protocollo CWNet e il timing del keyer hanno entrambi un riferimento reale, ma nessuno aveva mai costruito il modo di verificare l'aderenza.

## Cosa esiste ora

**[STRATEGY.md](../../../STRATEGY.md)** — scritto da zero con l'utente, sezione per sezione, con pushback. Il posizionamento è la frase che decide tutto: ogni comportamento che conta si dimostra contro un riferimento reale, non si rende simile. Il riferimento del protocollo è il software di DL4YHF; quello del keyer è il firmware K1EL K8 (l'utente ha corretto un proprio refuso: non K3LR). Le Boundaries sono taglienti e vanno lette prima di proporre lavoro: WireGuard è abbandonabile, i preset diversi dal K8 sono best effort non validati, niente OTA per ora, WebUI ferma. Frase-test: resistere quando l'unico argomento è "c'è e costa poco aggiungere".

**Suite host verde**, 189/189, plain e ASan/UBSan. Erano 8 rossi. Due erano **bug veri nel firmware**, non test invecchiati:
- `components/keyer_iambic/src/iambic.c` — il debounce di rilascio si autoattivava a t=0 perché i timestamp partivano da 0. Sul target blancava i paddle per 5 ms dopo il boot. Documentato in `docs/solutions/logic-errors/release-debounce-blanks-first-event.md`.
- Stesso file — `progress_pct` in Mode B troncato a `uint8_t`: un rilascio oltre il 255% wrappava e rientrava sotto la finestra di memoria.

Un terzo (`cwnet_frame.c`, cast su `payload_len`) è emerso solo compilando coi sanitizer, ed è il motivo per cui la CI ha due varianti.

**[.github/workflows/host-tests.yml](../../../.github/workflows/host-tests.yml)** — prima non esisteva nessuna CI di build o test, solo la build dell'immagine devcontainer.

**[docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md](../../../docs/plans/2026-09-01-2157-feat-banco-prova-cwnet-plan.md)** — il piano del banco. Da leggere per intero prima di toccare qualsiasi cosa di CWNet. Le parti che contano di più sono le Key Decisions e la tabella delle sette ipotesi in Dependencies.

## La scoperta che cambia il progetto

**Il sorgente del riferimento è scaricabile ed è attuale.** `Remote_CW_Keyer_Sources.zip` da qsl.net/dl4yhf, 1,4 MB, file datati fino al **20 ottobre 2025**. L'utente credeva di non avere accesso al sorgente corrente. Ce l'ha. Non serve Borland per leggerlo, solo per compilarlo.

Da lì, verificato leggendo `CwNet.h` e `CwStreamEnc.h`:

- `CWNET_CMD_MORSE 0x10`. Il nostro client manda il keying su `0x14`/`0x15`, che nel protocollo sono CI-V e spettro.
- Il payload MORSE è uno stream a 7 bit: bit 7 = stato del tasto, bit 6..0 = attesa *prima* di applicarlo. Noi mandiamo un timestamp assoluto a 4 byte. Sono due protocolli diversi.
- Il codec a 7 bit in `components/keyer_cwnet/src/cwnet_timestamp.c` **è già corretto** e coincide byte per byte col suo. Ha 312 righe di test. Non lo chiama nessuno: il TX non lo usa.
- Esiste un secondo key-up di fine "over" dopo ~10 dot o 500 ms che noi non emettiamo.
- La latenza: la formula istantanea `t2-t0` è identica alla nostra. La divergenza è **dopo** — lui la passa in un peak-hold asimmetrico (sale al picco, scende di 1/10 del divario per ping). Non è una misura, è un parametro di controllo del jitter buffer. Nel nostro firmware quel filtro non esiste.
- Il server senza radio **rimanda indietro il keying** ai client connessi. È il loop di prova, regalato dall'autore.

Attenzione: tutto questo viene da lettura di sorgente, **non da osservazione sul filo**. L'utente ha giustamente insistito che restano ipotesi (H1-H7 nel piano) finché una cattura non le conferma. Sospetta che in qualche punto il codice nostro possa essere più giusto del sorgente pubblicato, perché a suo tempo avevano ricostruito incoerenze con Wireshark.

## Cose che l'utente ha corretto e vanno ricordate

- **Niente ESP-IDF in cloud.** Avevo iniziato a installarlo; mi ha fermato e ho rimosso `/opt/esp`. Il firmware si compila sulla sua macchina.
- **Lo scopo di CWNet è il determinismo di quello che si invia, meno il PTT.** Quello che arriva deve essere identico a quello partito, con una tolleranza da mettere a punto. Il PTT si tara dopo i primi giri.
- **Avevo detto una cosa sbagliata** e va segnalato perché è controintuitiva: sostenevo che il test di determinismo passerebbe anche col command byte sbagliato. Falso — se il server non riconosce i frame non rimanda indietro nulla e il test fallisce subito. Quello che il loop *non* vede è l'impacchettamento: quanti eventi per frame, dove si taglia, la cadenza dei PING. Per quello serve la cattura ufficiale.
- **La manipolazione a mano è esclusa** come sorgente della sequenza di prova: sotto i 32 ms il codec risolve a 1 ms, due esecuzioni manuali non danno gli stessi byte.
- **Misurare a 293 µs non ha senso**, quindi la quantizzazione a ~10 ms del nostro turnaround al PING (dal `vTaskDelay` del bg_task) non è un problema per un numero che dimensiona un buffer.
- **Il protocollo non definisce latenze massime**, quindi la strumentazione del PING non è un gate.
- **Una fixture presa dal nostro codice è deterministica per costruzione** e non prova niente sulla conformità. Da qui KTD5 nel piano: `reference/` porta gli attesi, `ours/` è diagnostica, `synthetic/` prova solo il comparatore. L'harness di U3 sarà verde molto prima delle catture vere, ed è lì che il verde sintetico rischia di essere scambiato per conformità.

## Blocchi

Due issue, etichettate `blocking`, aperte su richiesta esplicita dell'utente:

- **[#7](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/7)** — cosa deve contenere la sequenza nota, cosa significa "identico", quale tolleranza. L'utente ha detto che è il punto cruciale e merita un brainstorming a parte: da lì dipende la qualità di tutto.
- **[#8](https://github.com/iu3qez/RemoteCWKeyer-esp32/issues/8)** — cosa si registra accanto a una cattura perché fra un anno sia arbitrabile.

L'utente ha chiesto di "scrivere col sangue che le issue bloccanti bloccano". La regola sta in [CLAUDE.md](../../../CLAUDE.md#a-blocking-issue-blocks), sotto Critical Constraints: non si pianifica intorno, non si sostituisce con assunzioni, non si procede marcando il lavoro provvisorio. Applicata anche al piano stesso, che per questo resta `artifact_readiness: requirements-only` invece di dichiararsi pronto.

## Stato per unità del piano

| Unità | Stato |
|---|---|
| U1 correggere `register_postdissector` in `tools/wireshark/cwnet.lua` | sbloccata, non iniziata |
| U2 catena di estrazione pcap → header C | sbloccata, non iniziata |
| U3 harness di replay in `test_host` | sbloccata, non iniziata, dipende dal formato di U2 |
| U4 strumentazione turnaround PING | sbloccata, non iniziata |
| U5 sessione zero | **bloccata** da #7 e #8 |
| U6 i due confronti reali | **bloccata** da U5 e #7 |

R13 e R14 nel piano (passaggio del TX a `MORSE 0x10`, filtro peak-hold) sono lavoro della track "CWNet client", non del banco, e sono subordinati a H1/H2 e H5.

## Lavoro aperto oltre alle issue

- **La doc-review sulle sezioni di implementazione del piano non è stata eseguita.** La review a cinque revisori ha coperto il Product Contract, non il Planning Contract né le unità. La skill `ce-plan` la considera obbligatoria.
- **`CLAUDE.md` non menziona `docs/solutions/`.** Una riga renderebbe le lezioni trovabili dalle sessioni future. Non l'ho aggiunta perché `ce-compound` in modalità lightweight non tocca i file di istruzioni; l'ho proposta all'utente e non ha ancora risposto.
- **`CONCEPTS.md` non esiste.** La cattura del vocabolario è rimandata a un run completo di `ce-compound`.
- **`.devcontainer/Dockerfile` è fermo a `ARG DOCKER_TAG=v5.5.1`** con path `idf5.5_py3.12_env` hardcoded, non aggiornato dopo la migrazione a IDF v6. Annotato in `.claude/code-quality.md`, non toccato: va provato su un'immagine `espressif/idf:v6.x` reale, cosa impossibile da qui.
- Altre lezioni candidate per `ce-compound`, non ancora scritte: il troncamento di `progress_pct` a uint8, e il dissector come postdissector (quest'ultima solo dopo che U1 la risolve).

## Verifiche fatte

`cd test_host && cmake -B build -G Ninja && cmake --build build && ./build/test_runner` → 189/189. Stessa cosa con `-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` → 189/189, zero report dai sanitizer. CI verde su entrambe le voci della matrice.

`tshark`, `tcpdump` ed `editcap` **non sono installati** in questo container, quindi l'assunzione su `tshark -q -z follow,tcp,raw` nel piano non è stata provata empiricamente. È dichiarata come assunzione da verificare al primo uso.

## Come continuare

**Da riprendere per primo, per volontà esplicita dell'utente:** il peso probatorio delle fixture, KTD5 nel piano. Chiudendo la sessione ha detto "domani ci torniamo su questo punto, molto importante". Il nocciolo: una fixture generata dal nostro codice torna verde per costruzione, e l'harness di U3 sarà verde molto prima che esista una cattura vera. La domanda aperta non è se la distinzione serva — è stabilita — ma quanto in là vada portata: come l'esito dichiara la categoria, se una fixture sintetica debba poter fallire la build quando qualcuno la usa come atteso, e se la stessa logica valga per il confronto di determinismo, dove entrambi i capi vengono da `ours/`.

Poi, due strade, non alternative fra loro ma con ordini diversi:

1. **Sbloccare #7 con il brainstorming dedicato**, che l'utente ha già detto di volere separato. È la strada che rende utile tutto il resto, perché senza non si può fare la sessione zero.
2. **Partire da U1-U4**, che non dipendono da nessuna issue e costruiscono l'infrastruttura che consumerà le fixture quando arriveranno. U3 si sviluppa contro una fixture sintetica proprio per non aspettare.

Skill utili: `ce-brainstorm` per #7, `ce-work` per le unità sbloccate (ma solo dopo aver notato che il piano è `requirements-only` di proposito), `ce-doc-review` per il debito segnalato sopra.
