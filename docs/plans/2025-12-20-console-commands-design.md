# Console Commands Design

**Data**: 2025-12-20
**Stato**: Approvato

## Overview

Console comandi via UART/seriale per configurazione e diagnostica del keyer. Architettura lazy (on-demand), zero heap allocation, integrata con il sistema parameters.yaml esistente.

## Architettura

### Posizionamento

- **Core 1** (best-effort) - nessun task dedicato
- **Lazy polling** - check UART RX nell'idle loop, esegue solo quando c'è input completo
- **Zero impatto su RT path** (Core 0)

```
┌─────────────────────────────────────────────────┐
│                    Core 1                        │
│  ┌───────────┐  ┌───────────┐  ┌─────────────┐  │
│  │  WiFi/BT  │  │  Decoder  │  │ Console     │  │
│  │   Stack   │  │           │  │ (lazy poll) │  │
│  └───────────┘  └───────────┘  └─────────────┘  │
│                                      │          │
│                              UART RX ▼          │
└─────────────────────────────────────────────────┘
```

### Buffer statici

```rust
const LINE_BUF_SIZE: usize = 64;
const HISTORY_DEPTH: usize = 4;

struct Console {
    line_buf: [u8; LINE_BUF_SIZE],
    line_pos: usize,
    history: [[u8; LINE_BUF_SIZE]; HISTORY_DEPTH],
    history_idx: usize,
    history_nav: usize,
}
```

Totale: ~320 byte statici. Zero heap.

### Prompt

```
RustKeyer v0.3.2-g1a2b3c4> _
```

Versione/git hash generato a compile-time da `build.rs`.

## Parsing

### Tokenizzazione

Split semplice su spazi. Massimo 4 token (comando + 3 argomenti).

```rust
struct ParsedCommand<'a> {
    cmd: &'a str,
    args: [Option<&'a str>; 3],
}
```

### Output

- **Successo**: silenzio (stile Unix)
- **Errore**: codice + descrizione breve

## Codici Errore

| Codice | Significato |
|--------|-------------|
| E01 | Unknown command |
| E02 | Invalid value |
| E03 | Missing argument |
| E04 | Out of range |
| E05 | Requires 'confirm' |
| E06 | NVS error |

Formato output: `E01: unknown command 'foo'`

## Comandi

### Tabella comandi

| Comando | Sintassi | Descrizione |
|---------|----------|-------------|
| `help` | `help [cmd]` | Lista comandi o dettaglio singolo |
| `?` | `?` | Alias per help |
| `<cmd> ?` | `log ?` | Help inline per comando specifico |
| `set` | `set <param> <value>` | Modifica parametro |
| `show` | `show [pattern]` | Mostra parametri (wildcard: `keyer*`) |
| `log` | `log` | Mostra livello log corrente |
| `log` | `log level * LEVEL` | Imposta livello per tutti i tag |
| `log` | `log level TAG LEVEL` | Imposta livello per tag specifico |
| `log` | `log *=L` | Formato compatto (E/W/I/D/T) |
| `debug` | `debug <tag> <level>` | Livelli log ESP-IDF |
| `debug` | `debug none` | Disabilita tutto il logging |
| `debug` | `debug * verbose` | Tutto al massimo |
| `debug` | `debug info` | Stato ring buffer RT |
| `diag` | `diag` | Mostra stato diagnostic logging |
| `diag` | `diag on\|off` | Abilita/disabilita RT diagnostic |
| `save` | `save` | Persiste configurazione su NVS |
| `reboot` | `reboot confirm` | Riavvio sistema |
| `factory-reset` | `factory-reset confirm` | Cancella NVS + reboot |
| `flash` | `flash` | Riavvia in bootloader per esptool |
| `uf2` | `uf2` | Alias per flash (UF2 bootloader) |
| `stats` | `stats` | Overview sistema |
| `stats` | `stats tasks` | Lista task per core |
| `stats` | `stats heap` | Dettaglio memoria |
| `stats` | `stats stream` | KeyingStream status |
| `stats` | `stats rt` | Metriche real-time path |
| `version` | `version` o `v` | Mostra versione firmware |

### Help inline

Ogni comando supporta `?` come primo argomento per mostrare l'help dettagliato:

```
> log ?
log - Set log level

Usage:
  log                 Show current level
  log level * LEVEL   Set all tags (ERROR/WARN/INFO/DEBUG/TRACE)
  log level TAG LEVEL Set specific tag
  log *=L             Compact: set all (E/W/I/D/T)
  log TAG=L           Compact: set tag

> help
Available commands:
  help           List commands or show help
  ...

Type 'help <cmd>' or '<cmd> ?' for details
```

### Comandi pericolosi

`reboot` e `factory-reset` richiedono argomento `confirm`:

```
> reboot
E05: requires 'confirm'
> reboot confirm
[sistema riavvia]
```

### Comando debug

```
debug none              → esp_log_level_set("*", ESP_LOG_NONE)
debug * verbose         → esp_log_level_set("*", ESP_LOG_VERBOSE)
debug wifi warn         → esp_log_level_set("wifi", ESP_LOG_WARN)
debug info              → mostra stato ring buffer RT
```

Output di `debug info`:
```
RT Log: 42/128 entries, 0 dropped
BE Log: 18/64 entries, 0 dropped
```

Livelli: `none`, `error`, `warn`, `info`, `debug`, `verbose`

### Comando stats

Output `stats` (overview):
```
uptime: 3d 04:22:15
heap: 142KB free (84%)
cpu: core0 12% core1 8%
stream: ok, lag 0
```

Output `stats tasks` (diviso per core):
```
=== Core 0 (RT) ===
NAME            CPU%  STACK  PRIO
rt_keyer        11.2  1024   24
IDLE0           88.8  512    0

=== Core 1 (BE) ===
NAME            CPU%  STACK  PRIO
console         0.1   512    5
decoder         2.3   768    8
wifi            5.4   2048   12
IDLE1           92.2  512    0
```

## Registry Parametri

### Struttura generata (codegen)

```rust
// src/generated/config_console.rs (auto-generato)

pub struct ParamDescriptor {
    pub name: &'static str,
    pub category: &'static str,
    pub get_fn: fn() -> ParamValue,
    pub set_fn: fn(ParamValue) -> Result<(), ConsoleError>,
    pub param_type: ParamType,
}

pub static PARAMS: &[ParamDescriptor] = &[
    ParamDescriptor {
        name: "wpm",
        category: "keyer",
        get_fn: || ParamValue::U16(CONFIG.wpm.load(Relaxed)),
        set_fn: |v| { CONFIG.wpm.store(v.as_u16()?, Relaxed); Ok(()) },
        param_type: ParamType::U16 { min: 5, max: 100 },
    },
    // ... tutti gli altri parametri
];

pub static CATEGORIES: &[&str] = &["keyer", "audio", "hardware", "timing", "system"];
```

### Categorie (da parameters.yaml)

| Category | Parametri |
|----------|-----------|
| `keyer` | wpm, iambic_mode, memory_window_us, weight |
| `audio` | sidetone_freq_hz, sidetone_volume, fade_duration_ms |
| `hardware` | gpio_dit, gpio_dah, gpio_tx |
| `timing` | ptt_tail_ms, tick_rate_hz |
| `system` | debug_logging, led_brightness |

### Wildcard matching

`show keyer*` filtra su `category.starts_with("keyer")` o `name.starts_with("keyer")`.

## History

Ring buffer per ultimi 4 comandi. Navigazione con frecce su/giù (escape sequences ANSI).

```rust
impl Console {
    fn history_push(&mut self, line: &[u8]);
    fn history_prev(&mut self);  // freccia su
    fn history_next(&mut self);  // freccia giù
}
```

Escape sequences: `\x1b[A` (up), `\x1b[B` (down).

## Tab Completion

Primo match alfabetico, tab successivi ciclano tra le opzioni.

```rust
struct TabState {
    prefix_len: usize,
    match_idx: usize,
    cycling: bool,
}
```

Completamento su:
1. **Comandi** - dopo primo token parziale
2. **Parametri** - dopo `set ` o `show `
3. **Categorie** - per wildcard

Reset cycling quando utente digita altro carattere.

## Struttura File

```
src/
├── console/
│   ├── mod.rs           # Console struct, main loop, UART I/O
│   ├── parser.rs        # Tokenizer, ParsedCommand
│   ├── commands.rs      # Handler per ogni comando
│   ├── history.rs       # Ring buffer history
│   ├── completion.rs    # Tab completion logic
│   └── error.rs         # ConsoleError enum, formatting
├── generated/
│   ├── config.rs        # (esistente)
│   ├── config_meta.rs   # (esistente)
│   ├── config_nvs.rs    # (esistente)
│   └── config_console.rs # NUOVO: ParamDescriptor array
```

## Estensione Codegen

`scripts/gen_config.py` genera anche `config_console.rs`:
- Array `PARAMS` con tutti i parametri
- Array `CATEGORIES` con nomi categorie univoci
- Costante `VERSION_STRING` con versione/git hash

## Integrazione

```rust
// main.rs o task best-effort
fn idle_poll() {
    if console.has_input() {
        if let Some(line) = console.read_line() {
            console.execute(&line);
        }
    }
}
```

## Dipendenze

Nessuna nuova dipendenza esterna. Solo API ESP-IDF già usate:
- `esp_log_level_set()` per debug
- `esp_restart()` per reboot
- `nvs_*` per save/factory-reset
- `vTaskGetRunTimeStats()` per stats tasks
- `uxTaskGetSystemState()` per info task
