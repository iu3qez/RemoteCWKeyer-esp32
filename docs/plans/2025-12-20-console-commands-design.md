# Console Commands Design

**Date**: 2025-12-20
**Status**: Approved

## Overview

Console commands over UART/serial for keyer configuration and diagnostics. Lazy (on-demand) architecture, zero heap allocation, integrated with the existing parameters.yaml system.

## Architecture

### Placement

- **Core 1** (best-effort) - no dedicated task
- **Lazy polling** - check UART RX in the idle loop, executes only when there is complete input
- **Zero impact on RT path** (Core 0)

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

### Static buffers

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

Total: ~320 static bytes. Zero heap.

### Prompt

```
RustKeyer v0.3.2-g1a2b3c4> _
```

Version/git hash generated at compile-time by `build.rs`.

## Parsing

### Tokenization

Simple split on spaces. Maximum 4 tokens (command + 3 arguments).

```rust
struct ParsedCommand<'a> {
    cmd: &'a str,
    args: [Option<&'a str>; 3],
}
```

### Output

- **Success**: silence (Unix style)
- **Error**: code + short description

## Error Codes

| Code | Meaning |
|--------|-------------|
| E01 | Unknown command |
| E02 | Invalid value |
| E03 | Missing argument |
| E04 | Out of range |
| E05 | Requires 'confirm' |
| E06 | NVS error |

Output format: `E01: unknown command 'foo'`

## Commands

### Command table

| Command | Syntax | Description |
|---------|----------|-------------|
| `help` | `help [cmd]` | List commands or show detail for one |
| `?` | `?` | Alias for help |
| `<cmd> ?` | `log ?` | Inline help for a specific command |
| `set` | `set <param> <value>` | Modify a parameter |
| `show` | `show [pattern]` | Show parameters (wildcard: `keyer*`) |
| `log` | `log` | Show current log level |
| `log` | `log level * LEVEL` | Set level for all tags |
| `log` | `log level TAG LEVEL` | Set level for a specific tag |
| `log` | `log *=L` | Compact format (E/W/I/D/T) |
| `debug` | `debug <tag> <level>` | ESP-IDF log levels |
| `debug` | `debug none` | Disable all logging |
| `debug` | `debug * verbose` | Everything at maximum |
| `debug` | `debug info` | RT ring buffer status |
| `diag` | `diag` | Show diagnostic logging status |
| `diag` | `diag on\|off` | Enable/disable RT diagnostic |
| `save` | `save` | Persist configuration to NVS |
| `reboot` | `reboot confirm` | Restart system |
| `factory-reset` | `factory-reset confirm` | Erase NVS + reboot |
| `flash` | `flash` | Restart into bootloader for esptool |
| `uf2` | `uf2` | Alias for flash (UF2 bootloader) |
| `stats` | `stats` | System overview |
| `stats` | `stats tasks` | List tasks per core |
| `stats` | `stats heap` | Memory detail |
| `stats` | `stats stream` | KeyingStream status |
| `stats` | `stats rt` | Real-time path metrics |
| `version` | `version` or `v` | Show firmware version |

### Inline help

Every command supports `?` as the first argument to show detailed help:

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

### Dangerous commands

`reboot` and `factory-reset` require the `confirm` argument:

```
> reboot
E05: requires 'confirm'
> reboot confirm
[system reboots]
```

### debug command

```
debug none              → esp_log_level_set("*", ESP_LOG_NONE)
debug * verbose         → esp_log_level_set("*", ESP_LOG_VERBOSE)
debug wifi warn         → esp_log_level_set("wifi", ESP_LOG_WARN)
debug info              → shows RT ring buffer status
```

Output of `debug info`:
```
RT Log: 42/128 entries, 0 dropped
BE Log: 18/64 entries, 0 dropped
```

Levels: `none`, `error`, `warn`, `info`, `debug`, `verbose`

### stats command

Output of `stats` (overview):
```
uptime: 3d 04:22:15
heap: 142KB free (84%)
cpu: core0 12% core1 8%
stream: ok, lag 0
```

Output of `stats tasks` (split by core):
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

## Parameter Registry

### Generated structure (codegen)

```rust
// src/generated/config_console.rs (auto-generated)

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
    // ... all the other parameters
];

pub static CATEGORIES: &[&str] = &["keyer", "audio", "hardware", "timing", "system"];
```

### Categories (from parameters.yaml)

| Category | Parameters |
|----------|-----------|
| `keyer` | wpm, iambic_mode, memory_window_us, weight |
| `audio` | sidetone_freq_hz, sidetone_volume, fade_duration_ms |
| `hardware` | gpio_dit, gpio_dah, gpio_tx |
| `timing` | ptt_tail_ms, tick_rate_hz |
| `system` | debug_logging, led_brightness |

### Wildcard matching

`show keyer*` filters on `category.starts_with("keyer")` or `name.starts_with("keyer")`.

## History

Ring buffer for the last 4 commands. Navigation with up/down arrows (ANSI escape sequences).

```rust
impl Console {
    fn history_push(&mut self, line: &[u8]);
    fn history_prev(&mut self);  // up arrow
    fn history_next(&mut self);  // down arrow
}
```

Escape sequences: `\x1b[A` (up), `\x1b[B` (down).

## Tab Completion

Approach "show-all": tab shows all options in a row (bash style).

Behavior:
- **Single match** → completes directly
- **Multiple matches** → prints all options on one line, completes the common prefix

Completion on:
1. **Commands** - after the first partial token
2. **Parameters** - after `set ` or `show `
3. **Categories** - for wildcard
4. **Debug args** - after `debug `: `info`, `none`, `*`, ESP_LOG tags, levels
5. **Diag args** - after `diag `: `on`, `off`

### Auto-generated ESP_LOG tags

The script `scripts/gen_log_tags.py` extracts the `static const char *TAG = "..."` tags from the code and generates `components/keyer_console/include/log_tags.h` during the build.

```
> debug <tab>
info none * config_nvs esp_netif esp_tls ... error warn debug verbose
> debug _
```

Example with a common prefix:
```
> show side<tab>
sidetone_freq_hz sidetone_vol
> show sidetone_
```

## File Structure

```
src/
├── console/
│   ├── mod.rs           # Console struct, main loop, UART I/O
│   ├── parser.rs        # Tokenizer, ParsedCommand
│   ├── commands.rs      # Handler for each command
│   ├── history.rs       # Ring buffer history
│   ├── completion.rs    # Tab completion logic
│   └── error.rs         # ConsoleError enum, formatting
├── generated/
│   ├── config.rs        # (existing)
│   ├── config_meta.rs   # (existing)
│   ├── config_nvs.rs    # (existing)
│   └── config_console.rs # NEW: ParamDescriptor array
```

## Codegen Extension

`scripts/gen_config.py` also generates `config_console.rs`:
- `PARAMS` array with all the parameters
- `CATEGORIES` array with unique category names
- `VERSION_STRING` constant with version/git hash

## Integration

```rust
// main.rs or best-effort task
fn idle_poll() {
    if console.has_input() {
        if let Some(line) = console.read_line() {
            console.execute(&line);
        }
    }
}
```

## Dependencies

No new external dependency. Only ESP-IDF APIs already in use:
- `esp_log_level_set()` for debug
- `esp_restart()` for reboot
- `nvs_*` for save/factory-reset
- `vTaskGetRunTimeStats()` for stats tasks
- `uxTaskGetSystemState()` for task info
