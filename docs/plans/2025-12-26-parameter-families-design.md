# Parameter Families Design

**Date:** 2025-12-26
**Status:** Approved

## Overview

Restructure `parameters.yaml` from flat list with comment-based categories to hierarchical families. Enables console commands like `show hardware.*` and prepares for GUI organization.

## Goals

- Console commands with pattern matching (`show`, `set`, `get`)
- Code generation with per-family structs
- GUI-ready metadata (labels, icons, ordering)
- Future extensibility

## YAML Structure (v2)

```yaml
version: 2  # Breaking change from v1

families:
  keyer:
    order: 1
    icon: "key"
    label:
      en: "Keyer"
      it: "Manipolatore"
    description:
      en: "Keying behavior and timing"
      it: "Comportamento e temporizzazione"
    aliases: [k]

    parameters:
      wpm:
        type: u16
        default: 25
        range: [5, 100]
        nvs_key: "wpm"
        runtime_change: idle_only
        priority: 1
        gui:
          label_short: { en: "WPM", it: "PPM" }
          label_long: { en: "Words Per Minute", it: "Parole Per Minuto" }
          description: { en: "Keying speed (PARIS)", it: "Velocita manipolazione (PARIS)" }
          widget: slider
          widget_config: { step: 1, tick_interval: 5 }
          advanced: false

      # ... other keyer parameters

    subfamilies:
      presets:
        is_composite: true
        schema_version: 1
        count: 10
        label:
          en: "Presets"
          it: "Preset"
        description:
          en: "Saved keyer configurations"
          it: "Configurazioni salvate"
        template:
          # ... preset fields

  audio:
    order: 2
    icon: "speaker"
    aliases: [a, snd]
    label: { en: "Audio", it: "Audio" }
    description: { en: "Sidetone and audio output", it: "Tono laterale e uscita audio" }
    parameters:
      sidetone_freq_hz: # ...
      sidetone_volume: # ...
      fade_duration_ms: # ...

  hardware:
    order: 3
    icon: "cpu"
    aliases: [hw, gpio]
    label: { en: "Hardware", it: "Hardware" }
    description: { en: "GPIO and pin configuration", it: "Configurazione GPIO e pin" }
    parameters:
      gpio_dit: # ...
      gpio_dah: # ...
      gpio_tx: # ...

  timing:
    order: 4
    icon: "clock"
    aliases: [t]
    label: { en: "Timing", it: "Temporizzazione" }
    description: { en: "RT loop and PTT timing", it: "Loop RT e temporizzazione PTT" }
    parameters:
      ptt_tail_ms: # ...
      tick_rate_hz: # ...

  system:
    order: 5
    icon: "settings"
    aliases: [sys]
    label: { en: "System", it: "Sistema" }
    description: { en: "Debug and system settings", it: "Debug e impostazioni sistema" }
    parameters:
      debug_logging: # ...
      led_brightness: # ...
```

## Family Metadata

Each family includes:

| Field | Type | Purpose |
|-------|------|---------|
| `order` | int | Display order in GUI/help |
| `icon` | string | Icon identifier for GUI |
| `label` | i18n | Human-readable name |
| `description` | i18n | Help text |
| `aliases` | list | Short names for console |
| `parameters` | map | Nested parameter definitions |
| `subfamilies` | map | Nested sub-families (optional) |

## Console Commands

### Syntax

```
show <pattern>     Display parameters matching pattern
set <path> <value> Set parameter value
get <path>         Get single parameter value
```

### Pattern Matching

```
keyer.wpm          Single parameter
keyer.*            All direct parameters in family
keyer.**           All parameters including subfamilies
hardware.*         All hardware parameters
**                 Everything
```

### Aliases

```
show hw.*          Equivalent to: show hardware.*
show k.*           Equivalent to: show keyer.*
show a.*           Equivalent to: show audio.*
```

### Help Output

```
> help show
show <pattern> - Display configuration parameters

Patterns:
  family.*    All parameters in family
  family.**   All parameters including subfamilies
  path        Single parameter

Families:
  keyer (k)      Keying behavior and timing
  audio (a,snd)  Sidetone and audio output
  hardware (hw)  GPIO and pin configuration
  timing (t)     RT loop and PTT timing
  system (sys)   Debug and system settings

Examples:
  show keyer.*         All keyer parameters
  show hw.**           All hardware (including nested)
  show keyer.presets.* All presets
```

## Code Generation

### Per-Family Structs

```c
// config_keyer.h
typedef struct {
    uint16_t wpm;
    iambic_mode_t iambic_mode;
    memory_mode_t memory_mode;
    squeeze_mode_t squeeze_mode;
    uint8_t weight;
    uint8_t mem_window_start_pct;
    uint8_t mem_window_end_pct;
} config_keyer_t;

// config_audio.h
typedef struct {
    uint16_t sidetone_freq_hz;
    uint8_t sidetone_volume;
    uint8_t fade_duration_ms;
} config_audio_t;

// config.h - composite
typedef struct {
    config_keyer_t keyer;
    config_audio_t audio;
    config_hardware_t hardware;
    config_timing_t timing;
    config_system_t system;
} config_t;

// Access: g_config.keyer.wpm, g_config.audio.sidetone_freq_hz
```

### Console Metadata

```c
// config_meta.c
static const config_family_t g_families[] = {
    { "keyer", "k", "Keying behavior", keyer_params, ARRAY_SIZE(keyer_params) },
    { "audio", "a,snd", "Audio output", audio_params, ARRAY_SIZE(audio_params) },
    { "hardware", "hw,gpio", "GPIO config", hw_params, ARRAY_SIZE(hw_params) },
    { "timing", "t", "RT and PTT timing", timing_params, ARRAY_SIZE(timing_params) },
    { "system", "sys", "System settings", sys_params, ARRAY_SIZE(sys_params) },
};

static const config_param_t keyer_params[] = {
    { "wpm", PARAM_U16, offsetof(config_t, keyer.wpm), 5, 100, "Words per minute" },
    { "iambic_mode", PARAM_ENUM, offsetof(config_t, keyer.iambic_mode), ... },
    // ...
};
```

### GUI Export (JSON)

```json
{
  "version": 2,
  "families": [
    {
      "id": "keyer",
      "order": 1,
      "icon": "key",
      "label": { "en": "Keyer", "it": "Manipolatore" },
      "parameters": ["wpm", "iambic_mode", "memory_mode", "squeeze_mode", "weight"],
      "subfamilies": ["presets"]
    }
  ],
  "parameters": {
    "wpm": {
      "family": "keyer",
      "type": "u16",
      "range": [5, 100],
      "default": 25,
      "widget": "slider"
    }
  }
}
```

## Migration

### Steps

1. **Convert YAML** (`scripts/migrate_params_v2.py`):
   - Read v1 flat structure
   - Group by `category` into families
   - Move `iambic_presets` under `keyer.subfamilies.presets`
   - Remove redundant `category` field
   - Output v2 structure

2. **Update codegen** (`scripts/gen_config_c.py`):
   - Detect `version: 2`
   - Generate per-family structs
   - Generate family metadata for console

3. **Update console** (`components/keyer_console`):
   - Implement pattern matching for `show`/`set`/`get`
   - Add family-aware help

### NVS Compatibility

- `nvs_key` values unchanged
- User data preserved across migration
- No NVS schema migration required

## Files Affected

| File | Change |
|------|--------|
| `parameters.yaml` | Complete restructure to v2 |
| `scripts/gen_config_c.py` | New v2 parser, per-family generation |
| `scripts/migrate_params_v2.py` | New migration tool (one-shot) |
| `components/keyer_config/*` | Regenerated (auto) |
| `components/keyer_console/src/cmd_show.c` | Pattern matching |
| `components/keyer_console/src/cmd_set.c` | Dot notation paths |
| `components/keyer_console/src/cmd_help.c` | Family documentation |

## Family Summary

```
families/
├── keyer (k)
│   ├── wpm, iambic_mode, memory_mode, squeeze_mode, weight, mem_window_*
│   └── subfamilies/
│       └── presets (schema_version, count, template)
├── audio (a, snd)
│   └── sidetone_freq_hz, sidetone_volume, fade_duration_ms
├── hardware (hw, gpio)
│   └── gpio_dit, gpio_dah, gpio_tx
├── timing (t)
│   └── ptt_tail_ms, tick_rate_hz
└── system (sys)
    └── debug_logging, led_brightness
```
