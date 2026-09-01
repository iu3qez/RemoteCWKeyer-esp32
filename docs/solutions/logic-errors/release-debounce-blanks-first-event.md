---
module: keyer_iambic
date: 2026-09-01
problem_type: logic_error
component: real_time_path
severity: medium
symptoms:
  - "Five iambic host tests fail: the FSM stays in IAMBIC_STATE_IDLE on the first tick with a paddle pressed"
  - "TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state) reports Expected 1 Was 0"
  - "On hardware, both paddles are ignored for the first 5 ms after boot or after iambic_reset()"
root_cause: uninitialized_sentinel
resolution_type: code_fix
related_components:
  - keyer_decoder
  - keyer_webui
tags:
  - debounce
  - timestamp
  - initialization
  - real-time
  - iambic
---

# Il debounce di rilascio che ingoia il primo evento

## Problema

Cinque test dell'iambic fallivano allo stesso modo: premuto un paddle al primo tick, la macchina a stati restava in `IAMBIC_STATE_IDLE` invece di partire con l'elemento.

```
test_main.c:36:test_iambic_dit:FAIL: Expected 1 Was 0
test_main.c:61:test_iambic_dah:FAIL: Expected 2 Was 0
test_main.c:87:test_iambic_mode_a_squeeze:FAIL: Expected TRUE Was FALSE
```

Sembravano test stale rispetto a un'implementazione evoluta. Non lo erano.

## Causa

`update_gpio()` in `components/keyer_iambic/src/iambic.c` applica un blanking dopo il rilascio, per sopprimere i rimbalzi del contatto in apertura:

```c
bool dit_in_blanking = (now_us - proc->dit_release_time_us) < IAMBIC_DEBOUNCE_RELEASE_US;
bool new_dit_pressed = raw_dit && !dit_in_blanking;
```

`iambic_init()` e `iambic_reset()` inizializzavano `dit_release_time_us` e `dah_release_time_us` a `0`. Con `now_us = 0` al primo tick, l'espressione diventa `0 - 0 < 5000`, cioè vera: la finestra di blanking risulta attiva **prima che qualunque rilascio sia mai avvenuto**, e il paddle viene ignorato.

Lo zero è ambiguo: significa sia "istante zero" sia "mai successo", e il confronto non può distinguerli.

Sul target l'effetto è modesto ma reale — i paddle restano sordi per 5 ms dopo il boot — mentre sui test, che partono deliberatamente da `now_us = 0`, è fatale.

## Soluzione

I timestamp "mai avvenuto" partono una finestra intera nel passato, così il confronto è falso per costruzione finché un rilascio vero non li aggiorna:

```c
/* No release has happened yet: park the timestamps one blanking period
 * in the past so the release debounce does not swallow a press at t=0. */
proc->dit_release_time_us = -IAMBIC_DEBOUNCE_RELEASE_US;
proc->dah_release_time_us = -IAMBIC_DEBOUNCE_RELEASE_US;
```

Applicato sia in `iambic_init()` sia in `iambic_reset()`. I cinque test passano senza toccarli.

## Due idiomi, e quando scegliere quale

Lo stesso schema — una guardia calcolata su un timestamp che potrebbe non essere mai stato scritto — compare altre due volte nel repo, ed entrambe erano già corrette con un idioma diverso, il sentinella esplicito:

```c
/* components/keyer_webui/src/api_config.c:151 */
if (last_save_us > 0 && (now_us - last_save_us) < (int64_t)10 * 1000000) {
```

```c
/* components/keyer_decoder/src/decoder.c:158 */
if (s_state != DECODER_STATE_RECEIVING || s_last_event_wall_us == 0) {
    return;
}
```

Il sentinella è più esplicito e va preferito dove la leggibilità conta più di tutto. Il valore parcheggiato nel passato evita un ramo e tiene l'espressione uniforme, il che ha senso in `update_gpio()`, che sta sul path hard-RT di Core 0 e calcola entrambe le finestre a ogni tick senza diramazioni.

Quello che non va bene è la terza via: inizializzare a zero e sperare che `now_us` sia abbastanza grande. Funziona finché qualcuno non chiama la funzione con un tempo piccolo — un test, un reset, il primo tick dopo il boot.

## Prevenzione

Quando scrivi una guardia della forma `(now - last_X) < WINDOW`, chiediti cosa vale `last_X` prima che X sia mai successo. Se la risposta è zero, la guardia è attiva all'avvio e sta sopprimendo l'evento che dovrebbe proteggere. Scegli il sentinella o il valore parcheggiato nel passato, mai lo zero implicito.

E un corollario di metodo: cinque test che falliscono tutti allo stesso modo su un componente maturo somigliano molto a test invecchiati. Prima di riscriverli, vale la pena leggere cosa asseriscono davvero — qui l'asserzione che falliva era sulla riga precedente a quella che sembrava.
