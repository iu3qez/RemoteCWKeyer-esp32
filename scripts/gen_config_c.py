#!/usr/bin/env python3
"""
keyer_c - Configuration Code Generator for C

Reads parameters.yaml and generates C code for:
- config.h: Atomic configuration struct
- config_meta.h: GUI metadata
- config_nvs.h: NVS persistence declarations
- config_console.h: Console parameter registry

Usage:
    python3 scripts/gen_config_c.py <parameters.yaml> <output_dir>

Example:
    python3 scripts/gen_config_c.py ../parameters.yaml components/keyer_config/include
"""

import yaml
import sys
from pathlib import Path
from typing import Dict, List, Any

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

        # Handle subfamilies (skip composites for now)
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


def main():
    """Main entry point"""
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <parameters.yaml> <output_dir>", file=sys.stderr)
        sys.exit(1)

    params_file = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])

    if not params_file.exists():
        print(f"ERROR: {params_file} not found", file=sys.stderr)
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Reading {params_file}...")
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
    if families:
        print(f"Found {len(families)} families: {[f['name'] for f in families]}")

    print("Generating config.h...")
    generate_config_h(params, families, output_dir)

    print("Generating config_meta.h...")
    generate_config_meta_h(params, output_dir)

    print("Generating config_nvs.h...")
    generate_config_nvs_h(params, output_dir)

    print("Generating config_console.h...")
    generate_config_console_h(params, families, output_dir)

    print(f"✓ Code generation complete: {output_dir}")


def get_c_atomic_type(param: Dict) -> str:
    """Map parameter type to C11 atomic type"""
    type_map = {
        'u8': 'atomic_uchar',
        'u16': 'atomic_ushort',
        'u32': 'atomic_uint',
        'bool': 'atomic_bool',
        'enum': 'atomic_uchar',
    }
    return type_map.get(param['type'], 'atomic_uint')


def get_c_storage_type(param: Dict) -> str:
    """Map parameter type to C storage type"""
    type_map = {
        'u8': 'uint8_t',
        'u16': 'uint16_t',
        'u32': 'uint32_t',
        'bool': 'bool',
        'enum': 'uint8_t',
    }
    return type_map.get(param['type'], 'uint32_t')


def get_default_value(param: Dict) -> tuple:
    """Get default value as C literal and optional comment"""
    if param['type'] == 'bool':
        return (str(param['default']).lower(), None)
    elif param['type'] == 'enum':
        enum_values = param['enum_values']
        default_enum = param['default']
        idx = enum_values.index(default_enum)
        return (str(idx), default_enum)
    else:
        return (str(param['default']), None)


def get_field_comment(param: Dict) -> str:
    """Generate field documentation comment"""
    gui = param['gui']
    label = gui['label_long']['en']
    range_str = ""
    if 'range' in param:
        range_str = f" ({param['range'][0]}-{param['range'][1]})"
    return f"{label}{range_str}"


def generate_config_h(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config.h with per-family structs (v2) or flat struct (v1)"""

    # Group params by family
    by_family = {}
    for p in params:
        family = p.get('family', 'misc')
        if family not in by_family:
            by_family[family] = []
        by_family[family].append(p)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.h
 * @brief Atomic configuration struct for keyer_c
 *
 * All parameters use atomic types for lock-free access from
 * multiple threads (RT thread, UI thread, WiFi thread, etc.)
 */

#ifndef KEYER_CONFIG_H
#define KEYER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

"""

    if families:
        # V2: Generate per-family structs
        for family in families:
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
        for family in families:
            fname = family['name']
            if fname in by_family and by_family[fname]:
                code += f"    config_{fname}_t {fname};\n"
        code += "    atomic_ushort generation;  /**< Config change counter */\n"
        code += "} keyer_config_t;\n\n"
    else:
        # V1: Flat struct
        code += """/**
 * @brief Global keyer configuration with atomic access
 */
typedef struct {
"""
        for p in params:
            atomic_type = get_c_atomic_type(p)
            comment = get_field_comment(p)
            code += f"    {atomic_type} {p['name']};  /**< {comment} */\n"
        code += "    atomic_ushort generation;  /**< Config generation counter */\n"
        code += "} keyer_config_t;\n\n"

    code += """/** Global configuration instance */
extern keyer_config_t g_config;

/**
 * @brief Initialize configuration with default values
 * @param cfg Configuration to initialize
 */
void config_init_defaults(keyer_config_t *cfg);

/**
 * @brief Increment generation counter to signal config change
 * @param cfg Configuration
 */
void config_bump_generation(keyer_config_t *cfg);

/* ============================================================================
 * Parameter Access Macros
 * ============================================================================ */

"""

    # Add accessor macros with family path for v2
    for p in params:
        name = p['name']
        family = p.get('family', None)
        upper = name.upper()

        if families and family:
            # V2: nested access
            code += f"#define CONFIG_GET_{upper}() \\\n"
            code += f"    atomic_load_explicit(&g_config.{family}.{name}, memory_order_relaxed)\n\n"
            code += f"#define CONFIG_SET_{upper}(v) do {{ \\\n"
            code += f"    atomic_store_explicit(&g_config.{family}.{name}, (v), memory_order_relaxed); \\\n"
            code += f"    config_bump_generation(&g_config); \\\n"
            code += f"}} while(0)\n\n"
        else:
            # V1: flat access
            code += f"#define CONFIG_GET_{upper}() \\\n"
            code += f"    atomic_load_explicit(&g_config.{name}, memory_order_relaxed)\n\n"
            code += f"#define CONFIG_SET_{upper}(v) do {{ \\\n"
            code += f"    atomic_store_explicit(&g_config.{name}, (v), memory_order_relaxed); \\\n"
            code += f"    config_bump_generation(&g_config); \\\n"
            code += f"}} while(0)\n\n"

    code += """#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_H */
"""

    with open(output_dir / "config.h", "w") as f:
        f.write(code)

    # Also generate config.c with initialization
    generate_config_c(params, families, output_dir)


def generate_config_c(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config.c with nested initialization (v2) or flat (v1)"""

    # Output to parent's src directory
    src_dir = output_dir.parent / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.c
 * @brief Configuration initialization
 */

#include "config.h"

keyer_config_t g_config;

void config_init_defaults(keyer_config_t *cfg) {
"""

    for p in params:
        name = p['name']
        family = p.get('family', None)
        default_val, comment = get_default_value(p)

        if families and family:
            # V2: nested path
            path = f"{family}.{name}"
        else:
            # V1: flat path
            path = name

        if comment:
            code += f"    atomic_init(&cfg->{path}, {default_val});  /* {comment} */\n"
        else:
            code += f"    atomic_init(&cfg->{path}, {default_val});\n"

    code += """    atomic_init(&cfg->generation, 0);
}

void config_bump_generation(keyer_config_t *cfg) {
    atomic_fetch_add_explicit(&cfg->generation, 1, memory_order_release);
}
"""

    with open(src_dir / "config.c", "w") as f:
        f.write(code)


def generate_config_meta_h(params: List[Dict], output_dir: Path):
    """Generate config_meta.h - GUI metadata"""

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_meta.h
 * @brief GUI metadata for all configuration parameters
 */

#ifndef KEYER_CONFIG_META_H
#define KEYER_CONFIG_META_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Widget type for parameter UI */
typedef enum {
    WIDGET_SLIDER = 0,
    WIDGET_SPINBOX = 1,
    WIDGET_DROPDOWN = 2,
    WIDGET_TOGGLE = 3,
    WIDGET_TEXTBOX = 4,
} widget_type_t;

/** When parameter changes can be applied */
typedef enum {
    RUNTIME_IMMEDIATE = 0,  /**< Applied immediately */
    RUNTIME_IDLE_ONLY = 1,  /**< Applied when paddles idle */
    RUNTIME_REBOOT = 2,     /**< Requires reboot */
} runtime_change_t;

/** Parameter metadata */
typedef struct {
    const char *name;
    const char *label_short_en;
    const char *label_long_en;
    const char *description_en;
    const char *category;
    uint8_t priority;
    bool advanced;
    widget_type_t widget;
    runtime_change_t runtime_change;
} param_meta_t;

/** Number of parameters */
#define PARAM_COUNT """ + str(len(params)) + """

/** All parameter metadata */
extern const param_meta_t PARAM_METADATA[PARAM_COUNT];

/** Get metadata by name */
const param_meta_t *config_get_meta(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_META_H */
"""

    with open(output_dir / "config_meta.h", "w") as f:
        f.write(code)


def generate_config_nvs_h(params: List[Dict], output_dir: Path):
    """Generate config_nvs.h - NVS persistence declarations"""

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_nvs.h
 * @brief NVS (Non-Volatile Storage) persistence for configuration
 */

#ifndef KEYER_CONFIG_NVS_H
#define KEYER_CONFIG_NVS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** NVS namespace for keyer configuration */
#define CONFIG_NVS_NAMESPACE "keyer_cfg"

/**
 * @brief Load all parameters from NVS
 * @return Number of parameters loaded, or negative on error
 */
int config_load_from_nvs(void);

/**
 * @brief Save all parameters to NVS
 * @return Number of parameters saved, or negative on error
 */
int config_save_to_nvs(void);

/**
 * @brief Load single parameter by name
 * @param name Parameter name
 * @return ESP_OK on success
 */
esp_err_t config_load_param(const char *name);

/**
 * @brief Save single parameter by name
 * @param name Parameter name
 * @return ESP_OK on success
 */
esp_err_t config_save_param(const char *name);

/* NVS key definitions */
"""

    for p in params:
        code += f"#define NVS_KEY_{p['name'].upper()} \"{p['nvs_key']}\"\n"

    code += """
#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_NVS_H */
"""

    with open(output_dir / "config_nvs.h", "w") as f:
        f.write(code)


def generate_config_console_h(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config_console.h with family metadata for v2"""

    # Get unique categories/families
    if families:
        categories = [f['name'] for f in families]
    else:
        categories = sorted(set(p.get('category', 'misc') for p in params))

    family_count = len(families) if families else 0

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.h
 * @brief Console command parameter registry with family support
 */

#ifndef KEYER_CONFIG_CONSOLE_H
#define KEYER_CONFIG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parameter value union */
typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    bool     b;
} param_value_t;

/** Parameter type with validation */
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
    const char *aliases;      /**< Comma-separated: "k" or "a,snd" */
    const char *description;
    uint8_t order;
} family_descriptor_t;

/** Parameter descriptor */
typedef struct {
    const char *name;
    const char *family;
    const char *full_path;    /**< "keyer.wpm" */
    param_type_t type;
    uint32_t min;
    uint32_t max;
    param_value_t (*get_fn)(void);
    void (*set_fn)(param_value_t);
} param_descriptor_t;

"""

    code += f"#define FAMILY_COUNT {family_count}\n"
    code += f"#define CONSOLE_PARAM_COUNT {len(params)}\n\n"

    if families:
        code += "extern const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT];\n"

    code += """extern const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT];

/** Find family by name or alias */
const family_descriptor_t *config_find_family(const char *name);

/** Find parameter by name or full path */
const param_descriptor_t *config_find_param(const char *name);

/** Get parameter value as string */
int config_get_param_str(const char *name, char *buf, size_t len);

/** Set parameter from string */
int config_set_param_str(const char *name, const char *value);

/** Pattern matching visitor callback */
typedef void (*param_visitor_fn)(const param_descriptor_t *param, void *ctx);

/**
 * @brief Visit all parameters matching pattern
 *
 * Patterns:
 * - "keyer.wpm"     exact match
 * - "keyer.*"       all direct params in keyer
 * - "keyer.**"      all params in keyer and subfamilies
 * - "hw.*"          alias expansion + wildcard
 */
void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_CONSOLE_H */
"""

    with open(output_dir / "config_console.h", "w") as f:
        f.write(code)


if __name__ == '__main__':
    main()
