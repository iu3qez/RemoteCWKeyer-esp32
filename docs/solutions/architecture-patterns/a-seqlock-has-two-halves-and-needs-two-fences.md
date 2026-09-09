---
title: "Un seqlock ha due metà, e servono due fence"
date: 2026-09-08
category: architecture-patterns
module: keyer_core
problem_type: architecture_pattern
component: real_time_path
severity: high
applies_when:
  - "Un producer e uno o più consumer condividono uno slot via un indice pubblicato con store/load atomico, come keying_stream_t, e il consumer valida la copia rileggendo l'indice"
  - "Uno snapshot di configurazione è protetto da un contatore di generazione riletto dopo la copia dei campi"
  - "Un test di stress passa in locale e sotto sanitizer ma una gamba CI su arm64 lo boccia in modo intermittente"
symptoms:
  - "3-6 campioni strappati accettati da test_stream_two_threads_never_accept_a_stale_or_torn_sample sotto ASan su Apple M1 (arm64), con un load acquire al posto della fence del lettore"
  - "1 e 80 campioni strappati sulla gamba CI arm64 plain (ubuntu-24.04-arm) senza la fence release dello scrittore, mai riprodotti su M1 né su x86-64"
  - "Il difetto è invisibile su x86-64 (TSO) e su alcune combinazioni toolchain/sanitizer, quindi un run verde locale non lo esclude"
root_cause: concurrency
resolution_type: code_fix
related_components:
  - main
  - test_host
tags:
  - seqlock
  - memory-model
  - fences
  - acquire-release
  - ring-buffer
  - arm64
  - stdatomic
  - real-time
---

# Un seqlock ha due metà, e servono due fence

## Contesto

`keying_stream_t` è l'unica interfaccia fra il produttore RT su Core 0 e i consumatori su
Core 1: un ring a potenza di due, un solo indice `write_idx`, nessun lock. La forma è quella
di un seqlock degenerato, dove il numero di sequenza è l'indice di scrittura stesso: il
consumatore copia lo slot e poi rilegge l'indice per decidere se la copia è ancora valida.

Il codice, prima di #57, sembrava corretto e passava tutta la suite host. `stream_write_slot()`
prendeva l'indice con `atomic_fetch_add_explicit(&write_idx, 1, memory_order_acq_rel)` e
memorizzava il campione *dopo*, quindi un consumatore che caricava `write_idx` nel mezzo vedeva
lag 1 e leggeva uno slot mezzo vecchio e mezzo nuovo; `stream_read()` accettava inoltre
`behind == capacity`, cioè proprio lo slot su cui il produttore sta per scrivere (issue #57,
sezione Evidence). Il difetto è uscito da una review avversariale, non da un test.

Il fix ovvio, store-then-publish con store release e `behind >= capacity` come overrun, non
è bastato. La prima versione rileggeva l'indice con un load acquire (session history): il test
di stress accettava ancora 3-6 campioni strappati sotto ASan su Apple M1, che è arm64. Chiusa
quella metà con la fence, la gamba `plain` arm64 della CI ha accettato 1 e 80 campioni
strappati in due run, mentre x86-64 e M1 non mostravano nulla (PR #70, sezione «What changes»).
Quel fallimento non si è riprodotto sulla macchina di sviluppo: il runner arm64 è stato il
giudice, non il portatile (session history). Il corpus `docs/solutions/` non aveva niente sul
modello di memoria: questa voce è quel buco.

## Guida

**Le due garanzie del C11, dette con precisione.**

- Un `atomic_load_explicit(..., memory_order_acquire)` ordina i load e gli store che stanno
  **dopo** di sé. Non ordina quelli **prima**: sul core debolmente ordinato un load precedente
  può completare dopo l'acquire load.
- Un `atomic_store_explicit(..., memory_order_release)` ordina i load e gli store che stanno
  **prima** di sé. Non ordina quelli **dopo**: gli store successivi possono diventare visibili
  prima del release store.

Da qui discendono le due metà del seqlock, e nessuna delle due è coperta dal tag di ordinamento
sull'operazione atomica: servono due `atomic_thread_fence()`.

**Metà lettore.** La rivalidazione ha senso solo se la copia del payload è già finita quando
la rilettura dell'indice avviene. L'acquire load *iniziale* non serve a questo, ordina ciò che
viene dopo. Fra copia e rilettura ci vuole `atomic_thread_fence(memory_order_acquire)`, e la
rilettura può allora essere `relaxed`. In `components/keyer_core/src/stream.c:144-156`:

```c
    stream_sample_t copy = stream->buffer[idx & stream->mask];

    atomic_thread_fence(memory_order_acquire);
    write = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    if (overrun_at(write, idx, stream->capacity)) {
        return false;
    }

    *out = copy;
```

Il predicato di scarto è `overrun_at()`, `components/keyer_core/src/stream.c:29-31`: `write - idx >= capacity`
in aritmetica wrapping. Il `>=` non è un dettaglio, è il secondo bug di #57: lo slot `capacity`
indietro rispetto alla posizione di scrittura è il *prossimo* del produttore, quindi già perso.
`consumer_resync()` atterra coerentemente a `capacity - 1` indietro (`components/keyer_core/src/stream.c:236-249`).

**Metà scrittore.** `write_idx == idx` è l'annuncio che lo slot `idx` sta per essere scritto, e
quell'annuncio è lo store release *precedente*. Un release store non ordina gli store che lo
seguono, quindi i byte dello slot possono diventare visibili prima dell'annuncio, e la
rivalidazione del lettore passerebbe su una copia strappata. Serve
`atomic_thread_fence(memory_order_release)` fra il publish precedente e lo store dello slot.
In `components/keyer_core/src/stream.c:72-82`:

```c
    size_t idx = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    size_t slot_idx = idx & stream->mask;

    atomic_thread_fence(memory_order_release);
    stream->buffer[slot_idx] = sample;

    atomic_store_explicit(&stream->write_idx, idx + 1, memory_order_release);
```

Il load rilassato di `write_idx` è lecito perché il produttore è uno solo: nessun altro muove
quell'indice. È la decisione presa dal maintainer in #57 e registrata come Amendment 003 in
`ARCHITECTURE.md:563-567`, con le regole riscritte in `ARCHITECTURE.md:81` (2.1.4, un solo
produttore), `:119` (3.1.2, fence release, store, publish) e `:121` (3.1.3, rilettura dietro
fence acquire). Store-then-publish con un solo contatore è sano *perché* il produttore è uno;
con più produttori servirebbe un numero di sequenza per slot, alternativa scartata in #70.

**Lo stesso idioma vale per qualsiasi snapshot protetto da generazione**, non solo per un ring.
`main/rt_task.c:189-212` rilegge la configurazione con lo stesso schema, copia dei campi, fence
acquire, rilettura rilassata di `g_config.generation`:

```c
            atomic_thread_fence(memory_order_acquire);
            uint16_t gen_after = atomic_load_explicit(&g_config.generation, memory_order_relaxed);
            if (gen_after != current_gen) {
                continue;  /* Torn read - retry next tick */
            }
```

Il commento sopra queste righe dice anche cosa il guard **non** prova, ed è la parte che si
dimentica: ogni setter bumpa la generazione *dopo* il proprio store, quindi uno store in volo è
invisibile alla rilettura, e un insieme misto di campi può essere applicato per un tick;
`last_config_gen` conserva il valore pre-lettura, così il bump che ha reso misto l'insieme forza
il ricaricamento al tick idle successivo. Ogni campo è un atomico a sé, quindi nessun valore è
mai strappato in sé. La prima stesura del commento vendeva il guard come protezione dell'intero
insieme; la review di #71 l'ha corretta (session history). Un guard che proteggesse davvero
l'*insieme* richiederebbe un bump pre-store sul writer, cioè un cambio di design di
`keyer_config` che nessuno ha chiesto.

**Il test deve rendere osservabile lo strappo.** Un campione che non porta informazione su di sé
non permette di distinguere una copia coerente da una mista: qui `sample_for_index()`
(`test_host/test_stream.c:128-134`) deriva tre campi distinti dall'indice, e `sample_is_index()`
li verifica tutti e tre, così una copia parziale o stantia si vede. Il pin è
`test_stream_two_threads_never_accept_a_stale_or_torn_sample`, `test_host/test_stream.c:194`:
produttore su un thread che non aspetta mai, 400k campioni su un ring da 64 slot, ogni campione
accettato deve essere quello che il suo indice dice. Due dettagli non decorativi: il loop di
stress ha un bound (`idle_turns > 100000000u`, riga 229) perché una regressione deve fallire e
non appendere il runner, e c'è un floor sul numero di campioni accettati (riga 239) che fa
fallire un run in cui i due thread si sono di fatto serializzati, cioè in cui il test non ha
testato nulla.

## Perché conta

`ARCHITECTURE.md:332`: «Corrupted CW timing is worse than silence». Un campione strappato è
esattamente quello. Uno strappo su un marker di silenzio consegna a chi conta i tick un conteggio
inventato, uno strappo su un campione consegna un fronte che non è mai esistito: il decoder
sbaglia, la manipolazione remota emette timing che nessuno ha prodotto, e non c'è FAULT perché
dal punto di vista del consumatore il dato è formalmente valido. È il modo di rompersi peggiore
possibile, silenzioso e plausibile.

E il fallimento è dipendente dalla piattaforma. x86-64 è TSO e non può fallire su una fence
mancante di questo tipo; su M1 il primo strappo si è visto solo sotto ASan, la cui
strumentazione rallenta il consumer abbastanza da aprire la finestra, e il secondo non si è visto
affatto. L'unica gamba che ha bocciato la versione a metà è stata `ubuntu-24.04-arm` senza
sanitizer (PR #70). Quindi: **una correzione di memory ordering non è dimostrata da un run verde
sulla macchina di sviluppo.** Il raddoppio dei job in `.github/workflows/host-tests.yml`
(matrice os × name, `ubuntu-latest` e `ubuntu-24.04-arm`, `plain` e `asan-ubsan`) si è ripagato
al primo utilizzo. Un dettaglio della matrice che ha rischiato di annullarlo: con il solo `os`
come asse, il secondo `include` sovrascriveva il primo e le gambe `plain` sparivano in silenzio;
`name` è un asse esplicito per questo (session history).

## Quando applicarla

- Qualsiasi struttura lock-free SPSC/SPMC in questo repository dove un payload viene validato
  da una lettura di indice o generazione **dopo** la copia. Oggi: `stream_read()`, e chiunque
  aggiunga un secondo ring con la stessa forma.
- Qualsiasi snapshot di configurazione protetto da un contatore di generazione, come in
  `rt_task.c`. Scrivere nel commento sia cosa il guard prova sia cosa non prova.
- Ogni volta che si è tentati di sostituire una fence con un tag di ordinamento su un load o
  uno store: chiedersi da quale lato della fence sta l'accesso da ordinare. Se sta dalla parte
  sbagliata, il tag non fa nulla.
- Come requisito di evidenza: una PR che tocca l'ordinamento di memoria è verde sulla gamba
  arm64, `plain` e `asan-ubsan`, o non è dimostrata.

## Esempi

I numeri osservati, dalla PR #70 e dall'handoff di sessione:

| metà mancante | piattaforma | campioni strappati accettati |
|---|---|---|
| fence acquire del lettore (load acquire al suo posto) | Apple M1 (arm64), ASan | 3-6 |
| fence release dello scrittore | arm64 CI, gamba `plain` | 1 e 80 in due run |
| fence release dello scrittore | x86-64 e Apple M1 | 0, mai riprodotto |
| entrambe (stato pre-#57, `fetch_add` prima dello store) | host, entrambe le varianti | 16327 |

La suite host è passata da 215 a 218 test con #70, invariata a 218 con #71 (`main/` non si
compila sull'host: quel call site è coperto solo dalla build del firmware).

## Related

- `components/keyer_core/src/stream.c`: le due fence, `overrun_at()`, `consumer_resync()`
- `main/rt_task.c:189-212`: la stessa fence acquire per lo snapshot di configurazione
- `test_host/test_stream.c:194`: il pin a due thread; `:142` e `:159` per overrun e resync
- `ARCHITECTURE.md`: regole 2.1.4, 3.1.2, 3.1.3, 3.2 e Amendment 003 (riga 563)
- `components/keyer_core/CLAUDE.md`: la versione breve delle due regole, e il monito
  «publish-after-store, not before» per chi tocca `stream.c`
- `CODING_STYLE.md`, «Memory ordering guide»: dice acquire per il consumer e release per il
  producer, senza questa avvertenza; questa voce è il caso in cui quella guida non basta
- Issue #57 e #69, PR #70 e #71 (mergiate il 2026-09-08)
- Boehm, *Can seqlocks get along with programming language memory models?* (2012), citato in #69
