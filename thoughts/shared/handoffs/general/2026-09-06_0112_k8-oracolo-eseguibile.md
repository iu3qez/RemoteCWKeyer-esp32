---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-05T23:12:13Z"
title: "Il K8 diventa un oracolo eseguibile, e la suite scopre di non essere ancorata"
summary: "Sbloccati #7/#8, corretto l'handshake CWNet inventato, il firmware ricompila, il riferimento K8 letto, emulato in gpsim e misurato; #32 contiene la specifica verificata e le decisioni del maintainer per il terzo squeeze_mode."
keywords: ["k8", "k1el", "gpsim", "iambic", "squeeze_mode", "issue-32", "oracolo-differenziale", "issue-sweep"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32"
resume_focus: "Implementare il terzo squeeze_mode (livello campionato agli istanti k·u, default) in TDD contro tracce K8 generate con gpsim, secondo #32."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "docs-unblock-u5-u6"
head: "34deae5"
---

# Il K8 diventa un oracolo eseguibile

Sessione del 5-6 settembre 2026, ripresa dall'handoff `2026-09-05_2145_sessione-zero-parziale.md`. Ha fatto quattro cose diverse in sequenza; le decisioni sono tutte sul tracker, questo file dice solo dove guardare e cosa è fragile.

## Come è andata, in ordine

1. **Sblocco.** Le due `blocking` (#7 sequenza/identico/tolleranza, #8 provenienza) sono **chiuse con decisione del maintainer** nei rispettivi commenti di chiusura. Nessuna `blocking` aperta. U5/U6 del piano banco-prova sono sbloccate; il piano resta `requirements-only` per #19 (doc-review mai fatta).
2. **Processo.** Merged: #22 (issue chiuse su evidenza, `/issue-sweep` prima dell'handoff, hook su entrambi i percorsi di `ce-handoff`, `.claude/` in convenzione, regola «una sola famiglia di skill: CE»; superpowers disinstallato a livello utente). Aperta: **PR #34** (STRATEGY: un boundary vieta di costruire, non di ricordare). Branch senza PR: `docs-unblock-u5-u6` (5 doc che dicevano ancora «bloccato»), `debt-iambic-config-vs-k8` (una riga in `code-quality.md`: doppio default di `squeeze_mode` in `parameters.yaml:120/260`).
3. **Ferro.** Il firmware **non linkava da maggio** — `gen_config_c.py` scriveva `config_nvs.c` in `include/src/`; fix merged (#28), build verificata su ESP-IDF **v6.0.2** (EIM: attivare con `. ~/.espressif/tools/activate_idf_v6.0.2.sh`, **non** `export.sh`; mai in pipe a `tail`, altrimenti le variabili muoiono nella subshell). Flashato: boot ok, WiFi ok. Bug console → #30 (editor: eco e stato decidono separatamente), #31 (nessun modo di vedere IP/stato; decisione: banner con `stats` alla connessione).
4. **CWNet.** `CWNET_CMD_WELCOME = 0x00` era inventato: il server conferma **rimandando il CONNECT con i permessi**. Fix merged (#27, chiude #23): READY sull'echo, permessi conservati, **rifiuto di manipolare senza TRANSMIT**. Scoperti #25 (il server arbitra *chi ha la chiave*, annunciato via `TX_INFO 0x05`, che scartiamo) e #26 (LED: solo libero/non libero, bassa priorità).
5. **Keyer — il grosso.** Audit della suite host: il progetto dichiara due riferimenti e la suite non ne aggancia nessuno; il K8 **non esisteva nel repo**. Poi è arrivato `morse8.zip`. Da lì: licenza (ridistribuzione permessa, **incompatibile GPL → fetch-non-vendor**), tre letture dell'assembly (analisi, review avversariale, confronto a tre), emulazione in gpsim, tre esperimenti misurati. **Tutto su #32.**

## Dove sta la verità

- **`#32`** — leggere i commenti **dal fondo verso l'alto**: ogni commento successivo corregge il precedente, e l'ultimo su ogni tema vince. In particolare: la specifica verificata del campionamento (una volta per **unità-dit**, non per elemento; livello, non fronte; stesso tipo cancellato *dopo* l'ultimo campione), il confronto a tre (cinque universali = «l'iambic», il resto varianti), le tre misure (`mark 84 566`, `space 84 382`, sidetone `1208` cicli), e le decisioni del maintainer.
- **`STRATEGY.md:62-64`** — la metrica: decisioni esatte, tempo tollerante, tolleranza scritta prima del test.
- **`components/keyer_iambic/src/iambic.c:200-232`** — il punto da modificare: oggi rileva un *fronte* (`dit_is_fresh`, `:224`) dentro una finestra percentuale, in entrambi i modi. `can_arm_dit/dah` (`:207-208`) fa già la memoria del solo opposto.
- **`test_host/test_iambic.c:88`** — l'asserzione vuota (`SEND_DIT || SEND_DAH`) da sostituire.

## Decisioni del maintainer, tutte su #32

- **WPM calcolati** (PARIS), non copiati dal K8; le durate K8 solo come fixture nei test, a velocità equivalente (TIMEBASE 70 ≈ 14,2 WPM). Il «5,7 % lento» è chiuso per il prodotto.
- **Terzo `squeeze_mode`, default**: livello agli istanti k·u agganciati al confine dell'elemento + cancella lo stesso tipo dopo l'ultimo campione. Approvato a condizione di costo non eccessivo — stimato: qualche decina di righe, sostituisce il test di finestra con un test di attraversamento di multiplo dell'unità. **Ortogonale a `IAMBIC_MODE_A/B`**, come nel K8: combinare il bonus element del K8 con l'osservazione dipendente dal modo (DL4YHF) darebbe una quarta cosa che non è nessuna delle tre.
- **La finestra percentuale resta** come estensione per le alte velocità, non come default. Default = K1EL.
- **Memoria del solo opposto è la definizione dell'iambic** (debouncer naturale) — non una particolarità del K8. Già così nel codice.
- Il K8 spedisce in **Mode B**; il suo A/B cambia solo il rilascio (B: un elemento in più; A: flush del latch). Legge il livello in **entrambi** i modi.
- **Niente è bloccato su DJ5IL**: `[Lit5]` si scrive come due casi con rilascio a 0,8u (→E) e 1,5u (→A), letti dall'emulatore. L'articolo, quando il maintainer l'avrà, può cambiare l'etichetta, non il test. Il maintainer sta raccogliendo materiale (articolo e codice) per conto suo.
- Test «squeeze a K»: da fermo il K8 manda **DIT** (→ R). Il test deve dire «dah, poi chiudi il dit».

## Il lavoro da fare, e come

TDD contro tracce gpsim. Ciclo: RED su `test_host` → GREEN in `iambic.c` → entrambe le varianti CI (`cmake -B build` e `build-asan` con `-fsanitize=address,undefined`). Test da scrivere, in ordine: (1) livello a k·u con tap fra due istanti → perso; (2) `[Lit4]` N/T; (3) `[Lit5]` due casi 0,8u/1,5u; (4) `[Lit6/7]` K/C con «dah poi dit»; (5) primo elemento in squeeze da fermo = DIT. Residuo da scrivere nella tolleranza: il nostro tick è 1 ms, il livello a k·u si legge al primo tick ≥ k·u.

Poi `tools/k8/` (puntatore archive.org **con timestamp dello snapshot**, sha256 `432df077…`, nota licenza + conflitto GPL, `ref/` ignorato) — mai vendorizzare.

## Machine-local, fragile

- `tmp/k8/` (ignorato): `morse8.asm` **originale, non toccare** (sha256 `432df077a197…`), `morse8_gpasm.asm` (una sola etichetta `CONFIG→CONFIGM`, perché gpasm la riserva), `morse8.hex/.cod`, `exp1-3*.stc/.log`, `parse_gpio_log.py`. Ricetta: `gpasm --mpasm-compatible -p p12c509 -o morse8.hex morse8_gpasm.asm`; `gpsim -i -p pic12c509 -c exp.stc morse8.hex`; pin: gpio0 DIT, gpio1 DAH, gpio2 KEY, gpio3 PB, gpio4 TONE; ingressi con pull-up, premuto = 0; il sign-on all'accensione dura fino al ciclo ~591 556 con TX squelchato — premere dopo.
- `morse8.zip` è **nella root del repo, non ignorato**: un `git add -A` lo committerebbe. Spostarlo o ignorarlo.
- `tmp/oracle/` (ignorato): le 32 catture DL4YHF, copiate fuori da `/tmp` che evapora.
- Sorgente DL4YHF completo e `cwnet_dump` compilato: `/private/tmp/claude-501/-Users-sf-Developer-RemoteCWKeyer-esp32/bed2bd1e-*/scratchpad/{dl4yhf,oracle}/` — **evapora al riavvio**; refetch in `tools/cwnet/README.md`.
- Terzo sorgente di confronto: `/Users/sf/Developer/deskhpsdr/src/iambic.c` (GPL-3), fuori dal repo.
- `gputils 1.5.2` e `gpsim 0.32.1` installati via brew questa sessione.

## Non fatto, e perché

- Nessuna riga di `squeeze_mode` scritta: la sessione si è fermata a specifica verificata + decisioni, a contesto pieno per metà. È il punto giusto per ripartire puliti.
- `[Lit5]` non scritto per la stessa ragione, non perché bloccato.
- `tools/k8/` non creato.
- Le PR di marzo (#2, #3) restano parcheggiate, non toccate.

## Verifica fatta

Host suite 191/191 verde, plain e ASan/UBSan, dopo #27. Firmware: `idf.py build` ok, `keyer_c.bin` 0x12a460, flashato, boot e WiFi ok (maintainer). Emulatore: tre esperimenti, ogni numero nella banda predetta o spiegato dal percorso di confine (~102 cicli). Sweep issue eseguito prima di questo handoff: nessuna chiudibile, #15 ristretta.

Skill utili: `ce-work` per #32, `issue-sweep` prima del prossimo handoff (l'hook lo ricorda su entrambi i percorsi — verificato dal vivo).
