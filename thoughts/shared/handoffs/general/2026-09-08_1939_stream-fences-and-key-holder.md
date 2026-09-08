---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T19:39:33Z"
title: "Chiave del server, seqlock dello stream, config re-read: tre PR mergiate, tre issue chiuse, la CI su arm64"
summary: "Sessione 8 settembre 2026 (pomeriggio): #25 (TX_INFO, chi ha la chiave), #57 (ordine di pubblicazione dello stream, due fence), #69 (re-read della config) mergiate come #66, #70, #71; gamba arm64 in CI che ha bocciato un fix a metà; sweep fatto, restano #26 narrowed e il lato stazione (#64, #65, #68) dell'altro handoff di oggi."
keywords: ["cwnet", "tx_info", "key-holder", "callsign", "stream", "seqlock", "fence", "acquire", "release", "arm64", "rt_task", "config", "issue-25", "issue-57", "issue-69", "issue-26", "issue-64", "issue-68", "issue-65"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/remotecwkeyer-issue-review-8ea3ab"
resume_focus: "Il lato stazione dall'handoff 2026-09-08_1000 (#64 daemon, #68 client host; GUI ferma su #65 blocking). In coda: #26 narrowed (LED binario già deciso dal maintainer, nome nella Web UI), e una voce docs/solutions sulle due fence del seqlock, che il corpus non ha."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "ca321ff"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/remotecwkeyer-issue-review-8ea3ab"
---

# Chiave del server, seqlock dello stream, config re-read

Ripresa da `2026-09-08_0912_cwnet-client-track.md`. In parallelo, un'altra sessione dello
stesso giorno ha rovesciato la strategia sul lato stazione e ha scritto
`2026-09-08_1000_station-daemon-start.md`: quello è l'handoff per #64, #65, #68; questo
copre il client e il core. Tutto è su `main` a `ca321ff`. Tre PR mergiate dal maintainer,
CI verde su dieci gambe: **#66** (#25), **#70** (#57), **#71** (#69). Suite host da 205 a 218.
Niente provato su hardware.

## Cosa esiste adesso, e dove

- **Chi ha la chiave** (#25, PR #66). `cwnet_client.c`: `handle_tx_info()` prende il frame
  `TX_INFO 0x05` (indice signed + callsign NUL) e deriva `cwnet_key_holder_t`:
  UNKNOWN / FREE / MINE / OTHER, come annunciato, senza timer nostro. «Mia» = indice ≥ 1 e
  callsign uguale a quello mandato nel CONNECT, che ora porta `g_config.system.callsign`
  (fallback username). Esposto da `cwnet_socket_get_key_holder()`, dalla riga di stato
  periodica di `bg_task` e da `key_holder` / `key_holder_name` in `/api/system`.
  Decisione del maintainer nel corpo di #25: la scatola manipola comunque e lo segnala.
  Fixture: i tre payload della cattura del 5 settembre in `test_host/cwnet_fixtures.h`.
- **Stream single-producer** (#57, PR #70). `stream.c`: fence release, store del campione,
  publish di `write_idx` con store release; `behind >= capacity` è overrun (lo slot
  `capacity` indietro è il prossimo del producer); `stream_read()` ricontrolla l'indice
  dietro una fence acquire dopo la copia e scarta; `consumer_resync()` atterra a
  `capacity - 1`. ARCHITECTURE.md 2.1.4, 3.1.2, 3.1.3, 3.2 riscritte, Amendment 003.
  Il blocco treecode di `keyer_core/CLAUDE.md` è rigenerato col cartographer del plugin.
- **Config re-read** (#69, PR #71). `rt_task.c`: fence acquire fra la copia dei campi e la
  rilettura rilassata della generazione. Il commento dice cosa prova davvero il guard: il
  setter bumpa *dopo* lo store, quindi un insieme misto passa per un tick idle e
  `last_config_gen` (valore pre-lettura) lo fa ricaricare al successivo. Tolto anche un
  `ESP_LOGI` one-shot su Core 0 in `hal_audio_write()`.
- **CI**. `host-tests.yml` gira la matrice plain / asan-ubsan su `ubuntu-latest` e
  `ubuntu-24.04-arm`, `timeout-minutes: 20`. `name` è un asse della matrice: con solo
  `os` come asse, il secondo `include` sovrascriveva il primo e le gambe plain sparivano.

## Cosa ha insegnato la sessione

- **Un seqlock ha due metà, e servono due fence.** Lettore: un load acquire non ordina i load
  *prima* di sé; fra copia e rilettura serve `atomic_thread_fence(acquire)` (3-6 campioni
  strappati sotto ASan su M1 senza). Writer: uno store release non ordina gli store *dopo*
  di sé; fra il publish precedente e i byte dello slot serve `atomic_thread_fence(release)`
  (1 e 80 campioni strappati sulla gamba plain arm64 di CI senza; mai visti su M1 né x86).
  Il test: `test_stream_two_threads_never_accept_a_stale_or_torn_sample`, ogni campione
  porta il proprio indice. Il corpus `docs/solutions/` non ha nulla sul modello di memoria:
  candidato a una voce (`ce-compound`).
- **La gamba arm64 ha bocciato un fix che in locale passava sempre.** Vale il raddoppio dei job.
- **Il nome in `TX_INFO` è il callsign, non lo username** (`CwNet.c:437`); il server al login
  confronta solo lo username (`CwNet_CheckUserAndGetPermissions`).
- **Review**: la mutazione fatta dal reviewer testing (worktree isolato) ha trovato il loop
  senza limite dello stress e il guard `index >= 1` non pinnato; l'adversarial su Opus con
  mandato di falsificare contro il sorgente resta il passaggio che paga.

## Sweep del 2026-09-08 sera

Chiuse con evidenza: #25, #57, #69. **#26 narrowed**: il punto 1 (client espone stato e
holder) è vero; restano i LED (binario libera / non libera, deciso dal maintainer il 5
settembre) e la Web UI col nome. Aperte e invariate: #54, #48, #46, #33, #19, #17; #64 e #68
(lato stazione, altro handoff); #65 `blocking` per la GUI del daemon.

Due finding P2 della review di #25 (twin callsign letto come «mia»; username sul filo senza
callsign) sono **wontfix del maintainer**, registrati in memoria: non riproporli.

## Pattern e lessico

Review avversariale su Opus prima della PR, sonnet per il resto; il modello va sempre
dichiarato. Il maintainer vuole **manipolare / manipolazione**; il blocco 0x06 è «la stringa
di controllo radio».

## Stato machine-local, fragile

- Sorgente DL4YHF: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9...`), estratto
  nello scratchpad di sessione (evapora). File CRLF: `grep -a`, `tr -d '\r'`.
- Catture in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/` (sessione 12 = keying,
  47 `TX_INFO`; sessione 10 = sysop).
- Artefatti delle tre review in `/tmp/compound-engineering-501/ce-code-review/`.
- Debito leggero in `.claude/code-quality.md`: blocchi treecode stantii (`test_host`,
  `keyer_cwnet`), clamp del marker di silenzio a 65,5 s. Da #70, non in issue:
  `consumer_resync()` si fa ri-sorpassare al push successivo (nessun chiamante di
  produzione); il `continue` del torn read in `rt_task` salta anche il delay del tick.
