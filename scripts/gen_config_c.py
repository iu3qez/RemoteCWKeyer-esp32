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


def generate_config_schema_json(params, families, output_dir: Path):
    """Generate JSON schema for WebUI."""
    import json

    schema = {"parameters": []}

    for param in params:
        p = {
            "name": param['full_path'],
            "type": param['type'].lower(),
            "widget": param.get('gui', {}).get('widget', 'spinbox'),
            "description": param.get('gui', {}).get('label_long', {}).get('en', param['name']),
        }

        if 'unit' in param:
            p['unit'] = param['unit']

        if 'range' in param:
            p['min'] = param['range'][0]
            p['max'] = param['range'][1]

        if param['type'].lower() == 'enum' and 'enum_values' in param:
            p['values'] = [
                {"name": v, "description": v}
                for v in param['enum_values']
            ]

        schema['parameters'].append(p)

    json_str = json.dumps(schema, indent=2)

    # Generate C header with embedded JSON
    header_content = f'''/* Auto-generated - DO NOT EDIT */
#ifndef CONFIG_SCHEMA_JSON_H
#define CONFIG_SCHEMA_JSON_H

static const char CONFIG_SCHEMA_JSON[] = R"JSON(
{json_str}
)JSON";

#endif
'''
    output_path = output_dir / 'config_schema.h'
    output_path.write_text(header_content)
    print(f"Generated {output_path}")


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

    print("Generating config_nvs.c...")
    generate_config_nvs_c(params, families, output_dir)

    print("Generating config_console.h...")
    generate_config_console_h(params, families, output_dir)

    print("Generating config_console.c...")
    generate_config_console_c(params, families, output_dir)

    print("Generating config_schema.h...")
    generate_config_schema_json(params, families, output_dir)

    print(f"✓ Code generation complete: {output_dir}")


def is_string_type(param: Dict) -> bool:
    """Check if parameter is a string type"""
    return param['type'] == 'string'


def get_string_max_length(param: Dict) -> int:
    """Get max_length for string parameter (includes null terminator space)"""
    return param.get('max_length', 32)


def get_c_atomic_type(param: Dict) -> str:
    """Map parameter type to C11 atomic type (or char[] for strings)"""
    if is_string_type(param):
        # Strings use char array, not atomic
        max_len = get_string_max_length(param)
        return f"char[{max_len + 1}]"  # +1 for null terminator

    type_map = {
        'u8': 'atomic_uchar',
        'u16': 'atomic_ushort',
        'u32': 'atomic_uint',
        'bool': 'atomic_bool',
        'enum': 'atomic_uchar',
    }
    return type_map.get(param['type'], 'atomic_uint')


def is_string_type(param: Dict) -> bool:
    """Check if parameter is a string type"""
    return param.get('type') == 'string'


def get_c_storage_type(param: Dict) -> str:
    """Map parameter type to C storage type"""
    if is_string_type(param):
        return 'const char *'

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
    elif param['type'] == 'string':
        default_str = param.get('default', '')
        return (f'"{default_str}"', None)
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
#include <string.h>

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
                comment = get_field_comment(p)
                if is_string_type(p):
                    max_len = get_string_max_length(p)
                    code += f"    char {p['name']}[{max_len + 1}];  /**< {comment} */\n"
                else:
                    atomic_type = get_c_atomic_type(p)
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
            comment = get_field_comment(p)
            if is_string_type(p):
                max_len = get_string_max_length(p)
                code += f"    char {p['name']}[{max_len + 1}];  /**< {comment} */\n"
            else:
                atomic_type = get_c_atomic_type(p)
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

    # Detect parameter name collisions across families
    name_counts = {}
    for p in params:
        n = p['name'].upper()
        name_counts[n] = name_counts.get(n, 0) + 1

    # Add accessor macros with family path for v2
    for p in params:
        name = p['name']
        family = p.get('family', None)
        upper = name.upper()
        # Only prefix with family when name collides across families
        if families and family and name_counts.get(upper, 0) > 1:
            macro_upper = f"{family.upper()}_{upper}"
        else:
            macro_upper = upper

        if is_string_type(p):
            max_len = get_string_max_length(p)
            if families and family:
                # V2: nested access for strings
                code += f"#define CONFIG_GET_{macro_upper}() \\\n"
                code += f"    (g_config.{family}.{name})\n\n"
                code += f"#define CONFIG_SET_{macro_upper}(v) do {{ \\\n"
                code += f"    strncpy(g_config.{family}.{name}, (v), {max_len}); \\\n"
                code += f"    g_config.{family}.{name}[{max_len}] = '\\0'; \\\n"
                code += f"    config_bump_generation(&g_config); \\\n"
                code += f"}} while(0)\n\n"
            else:
                # V1: flat access for strings
                code += f"#define CONFIG_GET_{macro_upper}() \\\n"
                code += f"    (g_config.{name})\n\n"
                code += f"#define CONFIG_SET_{macro_upper}(v) do {{ \\\n"
                code += f"    strncpy(g_config.{name}, (v), {max_len}); \\\n"
                code += f"    g_config.{name}[{max_len}] = '\\0'; \\\n"
                code += f"    config_bump_generation(&g_config); \\\n"
                code += f"}} while(0)\n\n"
        else:
            if families and family:
                # V2: nested access
                code += f"#define CONFIG_GET_{macro_upper}() \\\n"
                code += f"    atomic_load_explicit(&g_config.{family}.{name}, memory_order_relaxed)\n\n"
                code += f"#define CONFIG_SET_{macro_upper}(v) do {{ \\\n"
                code += f"    atomic_store_explicit(&g_config.{family}.{name}, (v), memory_order_relaxed); \\\n"
                code += f"    config_bump_generation(&g_config); \\\n"
                code += f"}} while(0)\n\n"
            else:
                # V1: flat access
                code += f"#define CONFIG_GET_{macro_upper}() \\\n"
                code += f"    atomic_load_explicit(&g_config.{name}, memory_order_relaxed)\n\n"
                code += f"#define CONFIG_SET_{macro_upper}(v) do {{ \\\n"
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
#include <string.h>

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

        if is_string_type(p):
            max_len = get_string_max_length(p)
            if comment:
                code += f"    strncpy(cfg->{path}, {default_val}, {max_len});  /* {comment} */\n"
            else:
                code += f"    strncpy(cfg->{path}, {default_val}, {max_len});\n"
            code += f"    cfg->{path}[{max_len}] = '\\0';\n"
        else:
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
        family = p.get('family', '')
        if family:
            # Include family prefix: NVS_LEDS_BRIGHTNESS
            code += f"#define NVS_{family.upper()}_{p['name'].upper()} \"{p['nvs_key']}\"\n"
        else:
            code += f"#define NVS_{p['name'].upper()} \"{p['nvs_key']}\"\n"

    code += """
#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_NVS_H */
"""

    with open(output_dir / "config_nvs.h", "w") as f:
        f.write(code)


def generate_config_nvs_c(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config_nvs.c - NVS persistence implementation"""

    src_dir = output_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_nvs.c
 * @brief NVS persistence implementation
 */

#include "config_nvs.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

int config_load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;  /* Namespace doesn't exist yet, use defaults */
    }
    if (err != ESP_OK) {
        return -1;
    }

    int loaded = 0;
    uint8_t u8_val;
    uint16_t u16_val;
    uint32_t u32_val;
    size_t str_len;

"""

    # Generate load code for each parameter
    for p in params:
        family = p.get('family', '')
        ptype = p['type']
        pname = p['name']

        # Build NVS key macro name
        if family:
            nvs_key = f"NVS_{family.upper()}_{pname.upper()}"
            config_path = f"g_config.{family}.{pname}"
        else:
            nvs_key = f"NVS_{pname.upper()}"
            config_path = f"g_config.{pname}"

        if ptype == 'u8' or ptype == 'enum':
            code += f"""    /* Load {family}.{pname} */
    if (nvs_get_u8(handle, {nvs_key}, &u8_val) == ESP_OK) {{
        atomic_store_explicit(&{config_path}, u8_val, memory_order_relaxed);
        loaded++;
    }}

"""
        elif ptype == 'u16':
            code += f"""    /* Load {family}.{pname} */
    if (nvs_get_u16(handle, {nvs_key}, &u16_val) == ESP_OK) {{
        atomic_store_explicit(&{config_path}, u16_val, memory_order_relaxed);
        loaded++;
    }}

"""
        elif ptype == 'u32':
            code += f"""    /* Load {family}.{pname} */
    if (nvs_get_u32(handle, {nvs_key}, &u32_val) == ESP_OK) {{
        atomic_store_explicit(&{config_path}, u32_val, memory_order_relaxed);
        loaded++;
    }}

"""
        elif ptype == 'bool':
            code += f"""    /* Load {family}.{pname} */
    if (nvs_get_u8(handle, {nvs_key}, &u8_val) == ESP_OK) {{
        atomic_store_explicit(&{config_path}, u8_val != 0, memory_order_relaxed);
        loaded++;
    }}

"""
        elif ptype == 'string':
            max_len = p.get('max_length', 32)
            code += f"""    /* Load {family}.{pname} */
    str_len = sizeof({config_path});
    if (nvs_get_str(handle, {nvs_key}, {config_path}, &str_len) == ESP_OK) {{
        loaded++;
    }}

"""

    code += """    nvs_close(handle);
    return loaded;
}

int config_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return -1;
    }

    int saved = 0;

"""

    # Generate save code for each parameter
    for p in params:
        family = p.get('family', '')
        ptype = p['type']
        pname = p['name']

        # Build NVS key macro name
        if family:
            nvs_key = f"NVS_{family.upper()}_{pname.upper()}"
            config_path = f"g_config.{family}.{pname}"
        else:
            nvs_key = f"NVS_{pname.upper()}"
            config_path = f"g_config.{pname}"

        if ptype == 'u8' or ptype == 'enum':
            code += f"""    /* Save {family}.{pname} */
    if (nvs_set_u8(handle, {nvs_key},
            atomic_load_explicit(&{config_path}, memory_order_relaxed)) == ESP_OK) {{
        saved++;
    }}

"""
        elif ptype == 'u16':
            code += f"""    /* Save {family}.{pname} */
    if (nvs_set_u16(handle, {nvs_key},
            atomic_load_explicit(&{config_path}, memory_order_relaxed)) == ESP_OK) {{
        saved++;
    }}

"""
        elif ptype == 'u32':
            code += f"""    /* Save {family}.{pname} */
    if (nvs_set_u32(handle, {nvs_key},
            atomic_load_explicit(&{config_path}, memory_order_relaxed)) == ESP_OK) {{
        saved++;
    }}

"""
        elif ptype == 'bool':
            code += f"""    /* Save {family}.{pname} */
    if (nvs_set_u8(handle, {nvs_key},
            atomic_load_explicit(&{config_path}, memory_order_relaxed) ? 1 : 0) == ESP_OK) {{
        saved++;
    }}

"""
        elif ptype == 'string':
            code += f"""    /* Save {family}.{pname} */
    if (nvs_set_str(handle, {nvs_key}, {config_path}) == ESP_OK) {{
        saved++;
    }}

"""

    code += """    err = nvs_commit(handle);
    nvs_close(handle);

    return (err == ESP_OK) ? saved : -1;
}

esp_err_t config_load_param(const char *name) {
    /* TODO: Implement single parameter load */
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t config_save_param(const char *name) {
    /* TODO: Implement single parameter save */
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}
"""

    with open(src_dir / "config_nvs.c", "w") as f:
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
    const char *str;  /**< For string parameters (points to config buffer) */
} param_value_t;

/** Parameter type with validation */
typedef enum {
    PARAM_TYPE_U8 = 0,
    PARAM_TYPE_U16 = 1,
    PARAM_TYPE_U32 = 2,
    PARAM_TYPE_BOOL = 3,
    PARAM_TYPE_ENUM = 4,
    PARAM_TYPE_STRING = 5,
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

    # Count all parameters (including strings)
    console_param_count = len(params)

    code += f"#define FAMILY_COUNT {family_count}\n"
    code += f"#define CONSOLE_PARAM_COUNT {console_param_count}\n\n"

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


def generate_config_console_c(params: List[Dict], families: List[Dict], output_dir: Path):
    """Generate config_console.c - Console parameter registry implementation"""

    src_dir = output_dir.parent / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    code = """/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.c
 * @brief Console parameter registry implementation
 */

#include "config_console.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

"""

    # Generate CONSOLE_FAMILIES array
    if families:
        code += "/* ============================================================================\n"
        code += " * Family Registry\n"
        code += " * ============================================================================ */\n\n"
        code += "const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT] = {\n"
        for f in families:
            aliases = ','.join(f.get('aliases', []))
            desc = f.get('description', {}).get('en', f['name'])
            order = f.get('order', 0)
            code += f'    {{ "{f["name"]}", "{aliases}", "{desc}", {order} }},\n'
        code += "};\n\n"

    # Generate get/set functions for each parameter
    code += "/* ============================================================================\n"
    code += " * Parameter Accessors\n"
    code += " * ============================================================================ */\n\n"

    for p in params:
        pname = p['name']
        family = p.get('family', '')
        ptype = p['type']

        # Build config path
        if family:
            config_path = f"g_config.{family}.{pname}"
        else:
            config_path = f"g_config.{pname}"

        # Function name uses underscore-separated full path
        if family:
            func_name = f"{family}_{pname}"
        else:
            func_name = pname

        # Generate getter
        code += f"static param_value_t get_{func_name}(void) {{\n"
        code += "    param_value_t v;\n"

        if ptype == 'string':
            code += f"    v.str = {config_path};\n"
        elif ptype == 'bool':
            code += f"    v.b = atomic_load_explicit(&{config_path}, memory_order_relaxed);\n"
        elif ptype in ('u8', 'enum'):
            code += f"    v.u8 = atomic_load_explicit(&{config_path}, memory_order_relaxed);\n"
        elif ptype == 'u16':
            code += f"    v.u16 = atomic_load_explicit(&{config_path}, memory_order_relaxed);\n"
        elif ptype == 'u32':
            code += f"    v.u32 = atomic_load_explicit(&{config_path}, memory_order_relaxed);\n"

        code += "    return v;\n"
        code += "}\n\n"

        # Generate setter
        code += f"static void set_{func_name}(param_value_t v) {{\n"

        if ptype == 'string':
            max_len = p.get('max_length', 32)
            code += f"    strncpy({config_path}, v.str, {max_len});\n"
            code += f"    {config_path}[{max_len}] = '\\0';\n"
        elif ptype == 'bool':
            code += f"    atomic_store_explicit(&{config_path}, v.b, memory_order_relaxed);\n"
        elif ptype in ('u8', 'enum'):
            code += f"    atomic_store_explicit(&{config_path}, v.u8, memory_order_relaxed);\n"
        elif ptype == 'u16':
            code += f"    atomic_store_explicit(&{config_path}, v.u16, memory_order_relaxed);\n"
        elif ptype == 'u32':
            code += f"    atomic_store_explicit(&{config_path}, v.u32, memory_order_relaxed);\n"

        code += "    config_bump_generation(&g_config);\n"
        code += "}\n\n"

    # Generate CONSOLE_PARAMS array
    code += "/* ============================================================================\n"
    code += " * Parameter Registry\n"
    code += " * ============================================================================ */\n\n"
    code += "const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {\n"

    for p in params:
        pname = p['name']
        family = p.get('family', '')
        ptype = p['type']

        # Build full_path
        if family:
            full_path = f"{family}.{pname}"
            func_name = f"{family}_{pname}"
        else:
            full_path = pname
            func_name = pname

        # Map type
        type_map = {
            'u8': 'PARAM_TYPE_U8',
            'u16': 'PARAM_TYPE_U16',
            'u32': 'PARAM_TYPE_U32',
            'bool': 'PARAM_TYPE_BOOL',
            'enum': 'PARAM_TYPE_ENUM',
            'string': 'PARAM_TYPE_STRING',
        }
        param_type = type_map.get(ptype, 'PARAM_TYPE_U32')

        # Get min/max
        if 'range' in p:
            min_val = p['range'][0]
            max_val = p['range'][1]
        elif ptype == 'enum':
            min_val = 0
            max_val = len(p.get('enum_values', [])) - 1
        elif ptype == 'bool':
            min_val = 0
            max_val = 1
        elif ptype == 'string':
            min_val = 0
            max_val = p.get('max_length', 32)
        else:
            min_val = 0
            max_val = 0xFFFFFFFF

        code += f'    {{ "{pname}", "{family}", "{full_path}", {param_type}, {min_val}, {max_val}, get_{func_name}, set_{func_name} }},\n'

    code += "};\n\n"

    # Generate helper functions
    code += """/* ============================================================================
 * Helper Functions
 * ============================================================================ */

const family_descriptor_t *config_find_family(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < FAMILY_COUNT; i++) {
        const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
        /* Check exact name match */
        if (strcmp(f->name, name) == 0) {
            return f;
        }
        /* Check aliases */
        if (f->aliases && f->aliases[0] != '\\0') {
            char aliases_copy[64];
            strncpy(aliases_copy, f->aliases, sizeof(aliases_copy) - 1);
            aliases_copy[sizeof(aliases_copy) - 1] = '\\0';
            char *alias = strtok(aliases_copy, ",");
            while (alias) {
                if (strcmp(alias, name) == 0) {
                    return f;
                }
                alias = strtok(NULL, ",");
            }
        }
    }
    return NULL;
}

const param_descriptor_t *config_find_param(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        /* Check full path (e.g., "keyer.wpm") */
        if (strcmp(p->full_path, name) == 0) {
            return p;
        }
        /* Check short name (e.g., "wpm") */
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int config_get_param_str(const char *name, char *buf, size_t len) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL || buf == NULL || len == 0) {
        return -1;
    }

    param_value_t v = p->get_fn();

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            snprintf(buf, len, "%u", v.u8);
            break;
        case PARAM_TYPE_U16:
            snprintf(buf, len, "%u", v.u16);
            break;
        case PARAM_TYPE_U32:
            snprintf(buf, len, "%lu", (unsigned long)v.u32);
            break;
        case PARAM_TYPE_BOOL:
            snprintf(buf, len, "%s", v.b ? "true" : "false");
            break;
        case PARAM_TYPE_STRING:
            snprintf(buf, len, "%s", v.str ? v.str : "");
            break;
        default:
            return -1;
    }
    return 0;
}

int config_set_param_str(const char *name, const char *value) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL || value == NULL) {
        return -1;
    }

    param_value_t v;
    unsigned long parsed;

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;  /* Out of range */
            }
            v.u8 = (uint8_t)parsed;
            break;
        case PARAM_TYPE_U16:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;
            }
            v.u16 = (uint16_t)parsed;
            break;
        case PARAM_TYPE_U32:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;
            }
            v.u32 = (uint32_t)parsed;
            break;
        case PARAM_TYPE_BOOL:
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) {
                v.b = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                       strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
                v.b = false;
            } else {
                return -3;  /* Invalid boolean */
            }
            break;
        case PARAM_TYPE_STRING:
            v.str = value;
            break;
        default:
            return -1;
    }

    p->set_fn(v);
    return 0;
}

void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx) {
    if (pattern == NULL || visitor == NULL) return;

    /* Check for wildcard patterns */
    const char *star = strchr(pattern, '*');

    if (star == NULL) {
        /* Exact match */
        const param_descriptor_t *p = config_find_param(pattern);
        if (p != NULL) {
            visitor(p, ctx);
        }
        return;
    }

    /* Get prefix (everything before the wildcard) */
    size_t prefix_len = (size_t)(star - pattern);
    char prefix[64];
    if (prefix_len >= sizeof(prefix)) {
        prefix_len = sizeof(prefix) - 1;
    }
    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\\0';

    /* Remove trailing dot from prefix if present */
    if (prefix_len > 0 && prefix[prefix_len - 1] == '.') {
        prefix[prefix_len - 1] = '\\0';
        prefix_len--;
    }

    /* Check if double-star (recursive) */
    bool recursive = (star[1] == '*');

    /* Iterate all params */
    for (int i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];

        /* Check if family matches prefix */
        if (prefix_len == 0) {
            /* Empty prefix matches all */
            visitor(p, ctx);
        } else if (strncmp(p->full_path, prefix, prefix_len) == 0) {
            /* Prefix matches */
            if (recursive) {
                /* ** matches all under this family */
                visitor(p, ctx);
            } else {
                /* * only matches direct children (no more dots after prefix) */
                const char *rest = p->full_path + prefix_len;
                if (rest[0] == '.') rest++;  /* Skip the dot */
                if (strchr(rest, '.') == NULL) {
                    visitor(p, ctx);
                }
            }
        }
    }
}
"""

    with open(src_dir / "config_console.c", "w") as f:
        f.write(code)


if __name__ == '__main__':
    main()
