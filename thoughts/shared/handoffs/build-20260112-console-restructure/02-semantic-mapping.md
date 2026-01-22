# Semantic Command Mapping

## Pattern: `<context> <verb> [noun] [value]`

---

## Context Mapping

| Context | Source | Description |
|---------|--------|-------------|
| `system` | system commands | version, reboot, stats, save |
| `keyer` | keyer.* config | wpm, mode, memory settings |
| `audio` | audio.* config | sidetone freq, volume, fade |
| `hardware` | hardware.* config | GPIO pins, ISR settings |
| `timing` | timing.* config | PTT tail, delays |
| `wifi` | wifi.* config | SSID, password, mode |
| `decoder` | decoder commands | CW decoder control |
| `text` | text keyer commands | send, memory slots |
| `log` | logging commands | USB log, ESP-IDF debug |
| `gpio` | diagnostic GPIO | read state, init |

---

## Complete Command Mapping

### Context: system

| Old | New | Notes |
|-----|-----|-------|
| `help` | `help` | Top-level, shows contexts |
| `help <cmd>` | `help <context>` | Context help |
| `?` | `help` | Alias removed |
| `version` | `system version` | |
| `v` | `system version` | Alias removed |
| `stats` | `system stats` | Overview |
| `stats heap` | `system stats heap` | |
| `stats tasks` | `system stats tasks` | |
| `stats stream` | `system stats stream` | |
| `stats rt` | `system stats rt` | |
| `save` | `system save` | NVS persist |
| `reboot` | `system reboot confirm` | |
| `factory-reset` | `system reset confirm` | |
| `uf2` / `flash` | `system boot uf2` | |

### Context: keyer

| Old | New | Notes |
|-----|-----|-------|
| `show keyer.*` | `keyer show` | All keyer params |
| `show keyer.wpm` | `keyer show wpm` | Single param |
| `set keyer.wpm 25` | `keyer set wpm 25` | |
| `set keyer.iambic_mode B` | `keyer set iambic_mode B` | |

### Context: audio

| Old | New | Notes |
|-----|-----|-------|
| `show audio.*` | `audio show` | All audio params |
| `show audio.sidetone_freq` | `audio show sidetone_freq` | |
| `set audio.sidetone_freq 700` | `audio set sidetone_freq 700` | |
| `set audio.volume 80` | `audio set volume 80` | |

### Context: hardware

| Old | New | Notes |
|-----|-----|-------|
| `show hardware.*` | `hardware show` | All hw params |
| `set hardware.dit_pin 4` | `hardware set dit_pin 4` | |

Alias: `hw` → `hardware`

### Context: timing

| Old | New | Notes |
|-----|-----|-------|
| `show timing.*` | `timing show` | All timing params |
| `set timing.ptt_tail_ms 200` | `timing set ptt_tail_ms 200` | |

### Context: wifi

| Old | New | Notes |
|-----|-----|-------|
| `show wifi.*` | `wifi show` | All wifi params |
| `set wifi.ssid "MyNet"` | `wifi set ssid "MyNet"` | |

### Context: decoder

| Old | New | Notes |
|-----|-----|-------|
| `decoder` | `decoder show` | Status |
| `decoder on` | `decoder on` | Enable |
| `decoder off` | `decoder off` | Disable |
| `decoder text` | `decoder text` | Show buffer |
| `decoder stats` | `decoder stats` | Timing stats |
| `decoder clear` | `decoder clear` | Reset |

### Context: text

| Old | New | Notes |
|-----|-----|-------|
| `send "CQ CQ"` | `text send "CQ CQ"` | |
| `m1` | `text play 1` | Send memory 1 |
| `m2` | `text play 2` | Send memory 2 |
| ... | ... | |
| `m8` | `text play 8` | Send memory 8 |
| `abort` | `text abort` | |
| `pause` | `text pause` | |
| `resume` | `text resume` | |
| `mem` | `text mem show` | List all slots |
| `mem 1` | `text mem show 1` | Show slot 1 |
| `mem 1 clear` | `text mem clear 1` | Clear slot 1 |
| `mem 1 label X` | `text mem label 1 X` | Set label |
| `mem 1 "text"` | `text mem set 1 "text"` | Set content |

### Context: log

| Old | New | Notes |
|-----|-----|-------|
| `log` | `log show` | Current levels |
| `log info` | `log set usb info` | USB log level |
| `log debug` | `log set usb debug` | |
| `log none` | `log set usb none` | |
| `debug info` | `log show esp` | ESP-IDF overview |
| `debug none` | `log set esp none` | Disable all |
| `debug <tag> <lvl>` | `log set esp <tag> <lvl>` | Per-tag |
| `diag on` | `log diag on` | RT diagnostics |
| `diag off` | `log diag off` | |

### Context: gpio

| Old | New | Notes |
|-----|-----|-------|
| `gpio` | `gpio show` | Read state |
| `gpio init` | `gpio init` | Reinit pins |
| `test cdc1` | `gpio test cdc1` | CDC test |
| `test gpio` | `gpio test` | GPIO test |
| `test log` | `log test` | Log test |

---

## Help System

### Level 0: `help`
```
Available contexts:
  system    System control (version, reboot, stats)
  keyer     Keyer settings (wpm, mode)
  audio     Audio settings (sidetone, volume)
  hardware  Hardware config (GPIO pins)
  timing    Timing settings (PTT tail)
  wifi      WiFi settings (SSID, mode)
  decoder   CW decoder control
  text      Text keyer (send, memory)
  log       Logging control
  gpio      GPIO diagnostics

Type 'help <context>' for details
```

### Level 1: `help keyer`
```
keyer - Keyer settings

Verbs:
  show [param]     Show parameter(s)
  set <param> <v>  Set parameter value

Parameters:
  wpm              Words per minute (5-60)
  iambic_mode      Mode A or B
  memory_mode      Standard or Contest
  squeeze_mode     Squeeze behavior
  ...

Examples:
  keyer show           Show all keyer settings
  keyer show wpm       Show current WPM
  keyer set wpm 25     Set WPM to 25
```

### Level 2: `help keyer set`
```
keyer set - Set keyer parameter

Usage: keyer set <param> <value>

Parameters:
  wpm <5-60>           Words per minute
  iambic_mode <A|B>    Iambic mode
  memory_mode <0-2>    Memory mode
  ...

Examples:
  keyer set wpm 25
  keyer set iambic_mode B
```

### Level 3: `help help`
```
help - Show help information

Usage:
  help              List all contexts
  help <context>    Show context verbs and parameters
  help <ctx> <verb> Show verb details
  help help         This message

Tab completion:
  Type partial text and press TAB for suggestions
  Works for contexts, verbs, parameters, and values
```

---

## Tab Completion Flow

```
<empty> TAB → [system, keyer, audio, hardware, timing, wifi, decoder, text, log, gpio, help]

keyer TAB → [show, set]

keyer set TAB → [wpm, iambic_mode, memory_mode, squeeze_mode, ...]

keyer set wpm TAB → [5-60] (show range)

decoder TAB → [show, on, off, text, stats, clear]

text TAB → [send, play, abort, pause, resume, mem]

text mem TAB → [show, set, clear, label]

text mem clear TAB → [1, 2, 3, 4, 5, 6, 7, 8]
```

---

## Implementation Notes

1. **Context dispatcher**: First word determines context handler
2. **Config contexts**: keyer/audio/hardware/timing/wifi share common show/set logic
3. **Aliases**: Only `hw` → `hardware` kept, all other aliases removed
4. **Backward compat**: None - breaking change, old syntax rejected
5. **Validation**: Same as before, per-parameter type/range checking

---

## Summary

| Metric | Old | New |
|--------|-----|-----|
| Top-level commands | 31 | 10 contexts |
| Max depth | 2 (cmd + subcmd) | 4 (ctx verb noun value) |
| Aliases | 5 (?, v, flash, m1-8) | 1 (hw) |
| Help levels | 1 | 3 |
| Tab completion | Commands + params | Full hierarchy |
