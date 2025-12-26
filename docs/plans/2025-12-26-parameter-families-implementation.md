# Parameter Families Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restructure parameters.yaml into hierarchical families with dot-notation console commands.

**Architecture:** YAML v2 with nested families → Python codegen → per-family C structs + console metadata. Console commands support `show keyer.*`, `show keyer.**`, aliases.

**Tech Stack:** Python 3, PyYAML, C11, ESP-IDF console

---

## Task 1: Create parameters_v2.yaml with Hierarchical Structure

**Files:**
- Create: `parameters_v2.yaml`

**Step 1: Write the new YAML structure**

Create `parameters_v2.yaml` with all families nested. This is a large file - structure it as:

```yaml
version: 2

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
          description:
            en: "Keying speed in words per minute (PARIS standard)"
            it: "Velocità di manipolazione in parole per minuto (standard PARIS)"
          widget: slider
          widget_config:
            step: 1
            tick_interval: 5
          advanced: false

      # Continue with: iambic_mode, memory_mode, squeeze_mode,
      # mem_window_start_pct, mem_window_end_pct, weight

    subfamilies:
      presets:
        is_composite: true
        schema_version: 1
        count: 10
        # ... copy existing iambic_presets content

  audio:
    order: 2
    icon: "speaker"
    aliases: [a, snd]
    label: { en: "Audio", it: "Audio" }
    description:
      en: "Sidetone and audio output"
      it: "Tono laterale e uscita audio"
    parameters:
      # sidetone_freq_hz, sidetone_volume, fade_duration_ms

  hardware:
    order: 3
    icon: "cpu"
    aliases: [hw, gpio]
    label: { en: "Hardware", it: "Hardware" }
    description:
      en: "GPIO and pin configuration"
      it: "Configurazione GPIO e pin"
    parameters:
      # gpio_dit, gpio_dah, gpio_tx

  timing:
    order: 4
    icon: "clock"
    aliases: [t]
    label: { en: "Timing", it: "Temporizzazione" }
    description:
      en: "RT loop and PTT timing"
      it: "Loop RT e temporizzazione PTT"
    parameters:
      # ptt_tail_ms, tick_rate_hz

  system:
    order: 5
    icon: "settings"
    aliases: [sys]
    label: { en: "System", it: "Sistema" }
    description:
      en: "Debug and system settings"
      it: "Debug e impostazioni sistema"
    parameters:
      # debug_logging, led_brightness
```

Copy all parameter definitions from current `parameters.yaml`, removing the `category` field.

**Step 2: Validate YAML syntax**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('parameters_v2.yaml'))" && echo "YAML valid"
```
Expected: `YAML valid`

**Step 3: Commit**

```bash
git add parameters_v2.yaml
git commit -m "feat(config): add parameters_v2.yaml with hierarchical families"
```

---

## Task 2: Update gen_config_c.py for V2 Parsing

**Files:**
- Modify: `scripts/gen_config_c.py`
- Test: Manual validation with `python3 scripts/gen_config_c.py parameters_v2.yaml /tmp/test_out`

**Step 1: Add version detection and v2 parser**

Add at top of `main()`:

```python
def main():
    # ... existing arg parsing ...

    with open(params_file) as f:
        schema = yaml.safe_load(f)

    version = schema.get('version', 1)
    print(f"Schema version: {version}")

    if version == 2:
        params, families = parse_v2_schema(schema)
    else:
        params = schema['parameters']
        families = None  # v1 has no family metadata

    print(f"Found {len(params)} parameters")
    # ... rest of generation ...
```

**Step 2: Implement parse_v2_schema()**

Add new function:

```python
def parse_v2_schema(schema: Dict) -> tuple:
    """Parse v2 hierarchical schema into flat params list and family metadata."""
    params = []
    families = []

    for family_name, family_data in schema.get('families', {}).items():
        # Extract family metadata
        family_meta = {
            'name': family_name,
            'order': family_data.get('order', 0),
            'icon': family_data.get('icon', ''),
            'label': family_data.get('label', {'en': family_name}),
            'description': family_data.get('description', {'en': ''}),
            'aliases': family_data.get('aliases', []),
        }
        families.append(family_meta)

        # Extract parameters with family path
        for param_name, param_data in family_data.get('parameters', {}).items():
            param = dict(param_data)
            param['name'] = param_name
            param['family'] = family_name
            param['full_path'] = f"{family_name}.{param_name}"
            params.append(param)

        # Handle subfamilies
        for sub_name, sub_data in family_data.get('subfamilies', {}).items():
            if sub_data.get('is_composite'):
                # Composite types handled separately
                continue
            for param_name, param_data in sub_data.get('parameters', {}).items():
                param = dict(param_data)
                param['name'] = param_name
                param['family'] = family_name
                param['subfamily'] = sub_name
                param['full_path'] = f"{family_name}.{sub_name}.{param_name}"
                params.append(param)

    # Sort families by order
    families.sort(key=lambda f: f['order'])

    return params, families
```

**Step 3: Run test generation**

Run:
```bash
python3 scripts/gen_config_c.py parameters_v2.yaml /tmp/test_out
ls -la /tmp/test_out/
```
Expected: Shows generated .h files

**Step 4: Commit**

```bash
git add scripts/gen_config_c.py
git commit -m "feat(codegen): add v2 schema parsing with families"
```

---

## Task 3: Generate Per-Family C Structs

**Files:**
- Modify: `scripts/gen_config_c.py` (function `generate_config_h`)

**Step 1: Update generate_config_h for per-family structs**

Replace `generate_config_h()` to generate nested structs:

```python
def generate_config_h(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config.h with per-family structs."""

    # Group params by family
    by_family = {}
    for p in params:
        family = p.get('family', 'misc')
        if family not in by_family:
            by_family[family] = []
        by_family[family].append(p)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
#ifndef KEYER_CONFIG_H
#define KEYER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

"""

    # Generate per-family structs
    for family in (families or [{'name': 'misc'}]):
        fname = family['name']
        fparams = by_family.get(fname, [])
        if not fparams:
            continue

        code += f"/** @brief {fname.title()} configuration */\n"
        code += f"typedef struct {{\n"
        for p in fparams:
            atomic_type = get_c_atomic_type(p)
            comment = get_field_comment(p)
            code += f"    {atomic_type} {p['name']};  /**< {comment} */\n"
        code += f"}} config_{fname}_t;\n\n"

    # Generate composite struct
    code += "/** @brief Complete keyer configuration */\n"
    code += "typedef struct {\n"
    for family in (families or [{'name': 'misc'}]):
        fname = family['name']
        if fname in by_family and by_family[fname]:
            code += f"    config_{fname}_t {fname};\n"
    code += "    atomic_ushort generation;  /**< Config change counter */\n"
    code += "} keyer_config_t;\n\n"

    code += """extern keyer_config_t g_config;
void config_init_defaults(keyer_config_t *cfg);
void config_bump_generation(keyer_config_t *cfg);

"""

    # Generate accessor macros with family prefix
    for p in params:
        name = p['name']
        family = p.get('family', 'misc')
        upper = name.upper()
        code += f"#define CONFIG_GET_{upper}() \\\n"
        code += f"    atomic_load_explicit(&g_config.{family}.{name}, memory_order_relaxed)\n\n"
        code += f"#define CONFIG_SET_{upper}(v) do {{ \\\n"
        code += f"    atomic_store_explicit(&g_config.{family}.{name}, (v), memory_order_relaxed); \\\n"
        code += f"    config_bump_generation(&g_config); \\\n"
        code += f"}} while(0)\n\n"

    code += """#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_H */
"""

    with open(output_dir / "config.h", "w") as f:
        f.write(code)

    generate_config_c(params, families, output_dir)
```

**Step 2: Update generate_config_c for nested access**

```python
def generate_config_c(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config.c with nested initialization."""
    src_dir = output_dir.parent / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
#include "config.h"

keyer_config_t g_config;

void config_init_defaults(keyer_config_t *cfg) {
"""

    for p in params:
        name = p['name']
        family = p.get('family', 'misc')
        default_val, comment = get_default_value(p)
        if comment:
            code += f"    atomic_init(&cfg->{family}.{name}, {default_val});  /* {comment} */\n"
        else:
            code += f"    atomic_init(&cfg->{family}.{name}, {default_val});\n"

    code += """    atomic_init(&cfg->generation, 0);
}

void config_bump_generation(keyer_config_t *cfg) {
    atomic_fetch_add_explicit(&cfg->generation, 1, memory_order_release);
}
"""

    with open(src_dir / "config.c", "w") as f:
        f.write(code)
```

**Step 3: Test generation produces valid C**

Run:
```bash
python3 scripts/gen_config_c.py parameters_v2.yaml /tmp/test_out
cat /tmp/test_out/config.h | head -80
```
Expected: Shows per-family structs like `config_keyer_t`, `config_audio_t`

**Step 4: Commit**

```bash
git add scripts/gen_config_c.py
git commit -m "feat(codegen): generate per-family C structs"
```

---

## Task 4: Generate Family Metadata for Console

**Files:**
- Modify: `scripts/gen_config_c.py` (function `generate_config_console_h`)

**Step 1: Add family_descriptor_t to config_console.h**

Update `generate_config_console_h()`:

```python
def generate_config_console_h(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config_console.h with family metadata."""

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
#ifndef KEYER_CONFIG_CONSOLE_H
#define KEYER_CONFIG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    bool     b;
} param_value_t;

typedef enum {
    PARAM_TYPE_U8 = 0,
    PARAM_TYPE_U16 = 1,
    PARAM_TYPE_U32 = 2,
    PARAM_TYPE_BOOL = 3,
    PARAM_TYPE_ENUM = 4,
} param_type_t;

/** Family descriptor */
typedef struct {
    const char *name;
    const char *aliases;  /* Comma-separated: "k" or "a,snd" */
    const char *description;
    uint8_t order;
} family_descriptor_t;

/** Parameter descriptor */
typedef struct {
    const char *name;
    const char *family;
    const char *full_path;  /* "keyer.wpm" */
    param_type_t type;
    uint32_t min;
    uint32_t max;
    param_value_t (*get_fn)(void);
    void (*set_fn)(param_value_t);
} param_descriptor_t;

"""

    code += f"#define FAMILY_COUNT {len(families) if families else 0}\n"
    code += f"#define CONSOLE_PARAM_COUNT {len(params)}\n\n"

    code += """extern const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT];
extern const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT];

const family_descriptor_t *config_find_family(const char *name);
const param_descriptor_t *config_find_param(const char *path);
int config_get_param_str(const char *path, char *buf, size_t len);
int config_set_param_str(const char *path, const char *value);

/* Pattern matching */
typedef void (*param_visitor_fn)(const param_descriptor_t *param, void *ctx);
void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_CONSOLE_H */
"""

    with open(output_dir / "config_console.h", "w") as f:
        f.write(code)
```

**Step 2: Test header generation**

Run:
```bash
python3 scripts/gen_config_c.py parameters_v2.yaml /tmp/test_out
grep -A5 "family_descriptor_t" /tmp/test_out/config_console.h
```
Expected: Shows family_descriptor_t struct

**Step 3: Commit**

```bash
git add scripts/gen_config_c.py
git commit -m "feat(codegen): add family metadata to config_console.h"
```

---

## Task 5: Update config_console.c with Family Registry

**Files:**
- Modify: `components/keyer_config/src/config_console.c`

**Step 1: Add family registry array**

Add after includes:

```c
/* Family registry */
const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT] = {
    { "keyer", "k", "Keying behavior and timing", 1 },
    { "audio", "a,snd", "Sidetone and audio output", 2 },
    { "hardware", "hw,gpio", "GPIO and pin configuration", 3 },
    { "timing", "t", "RT loop and PTT timing", 4 },
    { "system", "sys", "Debug and system settings", 5 },
};
```

**Step 2: Add config_find_family()**

```c
const family_descriptor_t *config_find_family(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < FAMILY_COUNT; i++) {
        const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
        /* Match by name */
        if (strcmp(f->name, name) == 0) {
            return f;
        }
        /* Match by alias */
        const char *aliases = f->aliases;
        while (*aliases) {
            const char *end = aliases;
            while (*end && *end != ',') end++;
            size_t len = (size_t)(end - aliases);
            if (strncmp(aliases, name, len) == 0 && name[len] == '\0') {
                return f;
            }
            aliases = (*end == ',') ? end + 1 : end;
        }
    }
    return NULL;
}
```

**Step 3: Update CONSOLE_PARAMS to include full_path**

Update each entry to add full_path:

```c
const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {
    { "wpm", "keyer", "keyer.wpm", PARAM_TYPE_U16, 5, 100, get_wpm, set_wpm },
    { "iambic_mode", "keyer", "keyer.iambic_mode", PARAM_TYPE_ENUM, 0, 1, get_iambic_mode, set_iambic_mode },
    // ... update all entries
};
```

**Step 4: Build to verify**

Run:
```bash
idf.py build 2>&1 | head -50
```
Expected: Build proceeds (may have other errors, but config_console.c compiles)

**Step 5: Commit**

```bash
git add components/keyer_config/src/config_console.c
git commit -m "feat(config): add family registry and find_family()"
```

---

## Task 6: Implement Pattern Matching (config_foreach_matching)

**Files:**
- Modify: `components/keyer_config/src/config_console.c`

**Step 1: Implement pattern matching logic**

Add function:

```c
/**
 * @brief Check if path matches pattern
 *
 * Patterns:
 * - "keyer.wpm"     exact match
 * - "keyer.*"       all direct params in keyer
 * - "keyer.**"      all params in keyer and subfamilies
 * - "hw.*"          alias expansion + wildcard
 */
static bool path_matches(const char *path, const char *pattern) {
    size_t pat_len = strlen(pattern);

    /* Check for ** (recursive) */
    if (pat_len >= 2 && strcmp(pattern + pat_len - 2, "**") == 0) {
        /* Match family prefix */
        return strncmp(path, pattern, pat_len - 2) == 0;
    }

    /* Check for * (single level) */
    if (pat_len >= 1 && pattern[pat_len - 1] == '*') {
        /* Match family.* */
        size_t prefix_len = pat_len - 1;
        if (strncmp(path, pattern, prefix_len) != 0) {
            return false;
        }
        /* Ensure no more dots after prefix (single level only) */
        const char *rest = path + prefix_len;
        return strchr(rest, '.') == NULL;
    }

    /* Exact match */
    return strcmp(path, pattern) == 0;
}

/**
 * @brief Expand alias in pattern
 *
 * "hw.*" -> "hardware.*"
 */
static void expand_pattern(const char *pattern, char *expanded, size_t len) {
    /* Find dot position */
    const char *dot = strchr(pattern, '.');
    if (dot == NULL) {
        strncpy(expanded, pattern, len);
        expanded[len - 1] = '\0';
        return;
    }

    /* Extract family part */
    size_t family_len = (size_t)(dot - pattern);
    char family_buf[32];
    if (family_len >= sizeof(family_buf)) {
        family_len = sizeof(family_buf) - 1;
    }
    memcpy(family_buf, pattern, family_len);
    family_buf[family_len] = '\0';

    /* Try to expand alias */
    const family_descriptor_t *f = config_find_family(family_buf);
    if (f != NULL) {
        snprintf(expanded, len, "%s%s", f->name, dot);
    } else {
        strncpy(expanded, pattern, len);
        expanded[len - 1] = '\0';
    }
}

void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx) {
    if (pattern == NULL || visitor == NULL) {
        return;
    }

    /* Expand aliases */
    char expanded[64];
    expand_pattern(pattern, expanded, sizeof(expanded));

    /* Visit matching params */
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        if (path_matches(p->full_path, expanded)) {
            visitor(p, ctx);
        }
    }
}
```

**Step 2: Build to verify**

Run:
```bash
idf.py build 2>&1 | tail -20
```
Expected: Build succeeds or shows unrelated errors

**Step 3: Commit**

```bash
git add components/keyer_config/src/config_console.c
git commit -m "feat(config): implement pattern matching with alias expansion"
```

---

## Task 7: Update show Command for Pattern Matching

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Rewrite cmd_show() to use config_foreach_matching**

Replace `cmd_show()`:

```c
/* Visitor callback for show command */
static void show_param_visitor(const param_descriptor_t *p, void *ctx) {
    (void)ctx;
    char buf[32];
    config_get_param_str(p->full_path, buf, sizeof(buf));
    printf("%s=%s\r\n", p->full_path, buf);
}

/**
 * @brief show [pattern] - Show parameters matching pattern
 *
 * Patterns:
 *   show                  All parameters
 *   show keyer.wpm        Single parameter
 *   show keyer.*          All keyer parameters
 *   show keyer.**         Keyer + subfamilies
 *   show hw.*             Alias for hardware.*
 */
static console_error_t cmd_show(const console_parsed_cmd_t *cmd) {
    const char *pattern = (cmd->argc > 0) ? cmd->args[0] : "**";

    /* Handle "show" with no args as "show **" (all params) */
    if (strcmp(pattern, "") == 0) {
        pattern = "**";
    }

    /* Use pattern matching */
    config_foreach_matching(pattern, show_param_visitor, NULL);

    return CONSOLE_OK;
}
```

**Step 2: Update USAGE_SHOW**

```c
static const char USAGE_SHOW[] =
    "  show                  All parameters\r\n"
    "  show keyer.*          All keyer parameters\r\n"
    "  show keyer.**         Keyer + subfamilies\r\n"
    "  show keyer.wpm        Single parameter\r\n"
    "  show hw.*             Alias for hardware.*\r\n"
    "\r\n"
    "Families:\r\n"
    "  keyer (k)      Keying behavior\r\n"
    "  audio (a,snd)  Sidetone output\r\n"
    "  hardware (hw)  GPIO pins\r\n"
    "  timing (t)     RT loop, PTT\r\n"
    "  system (sys)   Debug, LED";
```

**Step 3: Build and test on device**

Run:
```bash
idf.py build flash monitor
# In console:
# show keyer.*
# show hw.*
```
Expected: Shows filtered parameters

**Step 4: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): update show command with dot notation patterns"
```

---

## Task 8: Update set Command for Dot Notation

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Update cmd_set() to accept full paths**

```c
/**
 * @brief set <path> <value> - Set parameter by path
 *
 * Examples:
 *   set keyer.wpm 25
 *   set audio.sidetone_freq_hz 700
 *   set wpm 25  (legacy, still works)
 */
static console_error_t cmd_set(const console_parsed_cmd_t *cmd) {
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    const char *path = cmd->args[0];
    const char *value = cmd->args[1];

    /* Try full path first */
    int ret = config_set_param_str(path, value);

    /* If not found and no dot, try legacy name lookup */
    if (ret == -1 && strchr(path, '.') == NULL) {
        /* Search all params for matching name */
        for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
            if (strcmp(CONSOLE_PARAMS[i].name, path) == 0) {
                ret = config_set_param_str(CONSOLE_PARAMS[i].full_path, value);
                break;
            }
        }
    }

    switch (ret) {
        case 0:
            return CONSOLE_OK;
        case -1:
            return CONSOLE_ERR_UNKNOWN_CMD;
        case -2:
            return CONSOLE_ERR_INVALID_VALUE;
        case -4:
            return CONSOLE_ERR_OUT_OF_RANGE;
        default:
            return CONSOLE_ERR_INVALID_VALUE;
    }
}
```

**Step 2: Update config_set_param_str to use full_path**

In `config_console.c`, update `config_find_param()`:

```c
const param_descriptor_t *config_find_param(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        /* Match by full path (keyer.wpm) */
        if (strcmp(CONSOLE_PARAMS[i].full_path, path) == 0) {
            return &CONSOLE_PARAMS[i];
        }
        /* Also match by short name for backwards compat */
        if (strcmp(CONSOLE_PARAMS[i].name, path) == 0) {
            return &CONSOLE_PARAMS[i];
        }
    }
    return NULL;
}
```

**Step 3: Build and test**

Run:
```bash
idf.py build flash monitor
# In console:
# set keyer.wpm 30
# show keyer.wpm
```
Expected: Parameter is set and shows new value

**Step 4: Commit**

```bash
git add components/keyer_console/src/commands.c components/keyer_config/src/config_console.c
git commit -m "feat(console): update set command with dot notation paths"
```

---

## Task 9: Add help Command Family Documentation

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Update cmd_help() to show families**

Add family listing to help:

```c
static console_error_t cmd_help(const console_parsed_cmd_t *cmd) {
    if (cmd->argc > 0 && cmd->args[0] != NULL) {
        /* Check if asking about a family */
        const family_descriptor_t *f = config_find_family(cmd->args[0]);
        if (f != NULL) {
            printf("Family: %s\r\n", f->name);
            printf("Aliases: %s\r\n", f->aliases);
            printf("%s\r\n\r\n", f->description);
            printf("Parameters:\r\n");
            for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
                if (strcmp(CONSOLE_PARAMS[i].family, f->name) == 0) {
                    char buf[32];
                    config_get_param_str(CONSOLE_PARAMS[i].full_path, buf, sizeof(buf));
                    printf("  %s = %s\r\n", CONSOLE_PARAMS[i].full_path, buf);
                }
            }
            return CONSOLE_OK;
        }

        /* Help for specific command */
        const console_cmd_t *c = console_find_command(cmd->args[0]);
        if (c == NULL) {
            return CONSOLE_ERR_UNKNOWN_CMD;
        }
        show_command_help(c);
    } else {
        /* List all commands */
        size_t count;
        const console_cmd_t *cmds = console_get_commands(&count);
        printf("Commands:\r\n");
        for (size_t i = 0; i < count; i++) {
            printf("  %-14s %s\r\n", cmds[i].name, cmds[i].brief);
        }
        printf("\r\nFamilies (use 'help <family>'):\r\n");
        for (size_t i = 0; i < FAMILY_COUNT; i++) {
            printf("  %-14s (%s) %s\r\n",
                   CONSOLE_FAMILIES[i].name,
                   CONSOLE_FAMILIES[i].aliases,
                   CONSOLE_FAMILIES[i].description);
        }
        printf("\r\nType 'help <cmd>' or 'help <family>' for details\r\n");
    }
    return CONSOLE_OK;
}
```

**Step 2: Build and test**

Run:
```bash
idf.py build flash monitor
# In console:
# help
# help keyer
# help hw
```
Expected: Shows family documentation

**Step 3: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add family documentation to help command"
```

---

## Task 10: Replace parameters.yaml with V2

**Files:**
- Rename: `parameters.yaml` → `parameters_v1.yaml.bak`
- Rename: `parameters_v2.yaml` → `parameters.yaml`

**Step 1: Backup and rename**

```bash
mv parameters.yaml parameters_v1.yaml.bak
mv parameters_v2.yaml parameters.yaml
```

**Step 2: Regenerate all config code**

```bash
python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include
```

**Step 3: Update config_console.c getter/setter functions**

The get/set functions need to use the new nested paths. Update them to access `g_config.keyer.wpm` instead of `g_config.wpm`.

**Step 4: Full build and test**

```bash
idf.py fullclean
idf.py build flash monitor
```
Expected: Device boots, console works with new commands

**Step 5: Commit**

```bash
git add parameters.yaml parameters_v1.yaml.bak components/keyer_config/
git commit -m "feat(config): switch to v2 hierarchical parameters.yaml"
```

---

## Task 11: Add Tab Completion for Paths

**Files:**
- Modify: `components/keyer_console/src/completion.c`

**Step 1: Add path completion logic**

Add to completion.c:

```c
/* Complete parameter paths */
static size_t complete_param_path(const char *prefix,
                                   const char **completions,
                                   size_t max_completions) {
    size_t count = 0;
    size_t prefix_len = strlen(prefix);

    /* First try family names if no dot yet */
    if (strchr(prefix, '.') == NULL) {
        for (size_t i = 0; i < FAMILY_COUNT && count < max_completions; i++) {
            if (strncmp(CONSOLE_FAMILIES[i].name, prefix, prefix_len) == 0) {
                completions[count++] = CONSOLE_FAMILIES[i].name;
            }
        }
    }

    /* Then try full paths */
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT && count < max_completions; i++) {
        if (strncmp(CONSOLE_PARAMS[i].full_path, prefix, prefix_len) == 0) {
            completions[count++] = CONSOLE_PARAMS[i].full_path;
        }
    }

    return count;
}
```

**Step 2: Integrate with existing completion**

Update `console_complete()` to call `complete_param_path()` for show/set commands.

**Step 3: Build and test**

```bash
idf.py build flash monitor
# In console:
# show key<TAB>
# set keyer.<TAB>
```
Expected: Tab completion suggests paths

**Step 4: Commit**

```bash
git add components/keyer_console/src/completion.c
git commit -m "feat(console): add tab completion for parameter paths"
```

---

## Task 12: Final Integration Test

**Files:**
- Test manually on device

**Step 1: Test all patterns**

```bash
idf.py flash monitor
```

Test in console:
```
help
help keyer
help hw
show
show keyer.*
show keyer.**
show hw.*
show audio.sidetone_freq_hz
set keyer.wpm 30
set audio.sidetone_freq_hz 700
show keyer.wpm
save
```

**Step 2: Verify NVS persistence**

```
reboot confirm
show keyer.wpm
```
Expected: WPM still 30 after reboot

**Step 3: Test aliases**

```
show k.*
show a.*
show hw.*
show t.*
show sys.*
```
Expected: All show correct filtered output

**Step 4: Final commit**

```bash
git add -A
git commit -m "feat(config): complete parameter families implementation"
```

---

## Summary

| Task | Description |
|------|-------------|
| 1 | Create parameters_v2.yaml |
| 2 | Update codegen for v2 parsing |
| 3 | Generate per-family structs |
| 4 | Generate family metadata |
| 5 | Add family registry to config_console.c |
| 6 | Implement pattern matching |
| 7 | Update show command |
| 8 | Update set command |
| 9 | Update help command |
| 10 | Switch to v2 YAML |
| 11 | Add tab completion |
| 12 | Final integration test |
