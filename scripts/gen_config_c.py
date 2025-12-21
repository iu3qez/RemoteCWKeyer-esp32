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

    params = schema['parameters']
    print(f"Found {len(params)} parameters")

    print("Generating config.h...")
    generate_config_h(params, output_dir)

    print("Generating config_meta.h...")
    generate_config_meta_h(params, output_dir)

    print("Generating config_nvs.h...")
    generate_config_nvs_h(params, output_dir)

    print("Generating config_console.h...")
    generate_config_console_h(params, output_dir)

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


def generate_config_h(params: List[Dict], output_dir: Path):
    """Generate config.h - Atomic configuration struct"""

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

/**
 * @brief Global keyer configuration with atomic access
 *
 * Change detection via `generation` counter:
 * - Increment `generation` when any parameter changes
 * - Consumers check `generation` to detect updates
 */
typedef struct {
"""

    # Add fields
    for p in params:
        atomic_type = get_c_atomic_type(p)
        comment = get_field_comment(p)
        code += f"    {atomic_type} {p['name']};  /**< {comment} */\n"

    code += """    atomic_ushort generation;  /**< Config generation counter */
} keyer_config_t;

/** Global configuration instance */
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

    # Add accessor macros
    for p in params:
        name = p['name']
        upper = name.upper()
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
    generate_config_c(params, output_dir)


def generate_config_c(params: List[Dict], output_dir: Path):
    """Generate config.c - Default initialization (goes to src/)"""

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
        default_val, comment = get_default_value(p)
        if comment:
            code += f"    atomic_init(&cfg->{name}, {default_val});  /* {comment} */\n"
        else:
            code += f"    atomic_init(&cfg->{name}, {default_val});\n"

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


def generate_config_console_h(params: List[Dict], output_dir: Path):
    """Generate config_console.h - Console parameter registry"""

    # Get unique categories
    categories = sorted(set(p['category'] for p in params))

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.h
 * @brief Console command parameter registry
 */

#ifndef KEYER_CONFIG_CONSOLE_H
#define KEYER_CONFIG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

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

/** Parameter descriptor */
typedef struct {
    const char *name;
    const char *category;
    param_type_t type;
    uint32_t min;
    uint32_t max;
    param_value_t (*get_fn)(void);
    void (*set_fn)(param_value_t);
} param_descriptor_t;

/** Number of parameters */
#define CONSOLE_PARAM_COUNT """ + str(len(params)) + """

/** All parameter descriptors */
extern const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT];

/** Number of categories */
#define CATEGORY_COUNT """ + str(len(categories)) + """

/** All categories */
extern const char *CATEGORIES[CATEGORY_COUNT];

/** Find parameter by name */
const param_descriptor_t *config_find_param(const char *name);

/** Get parameter value as string */
int config_get_param_str(const char *name, char *buf, size_t len);

/** Set parameter from string */
int config_set_param_str(const char *name, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_CONSOLE_H */
"""

    with open(output_dir / "config_console.h", "w") as f:
        f.write(code)


if __name__ == '__main__':
    main()
