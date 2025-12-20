#!/usr/bin/env python3
"""
RustRemoteCWKeyer - Configuration Code Generator

Reads parameters.yaml and generates Rust code for:
- config.rs: Atomic configuration struct
- config_meta.rs: GUI metadata (labels, descriptions, widgets)
- config_nvs.rs: NVS persistence (load/save)

Usage:
    python3 scripts/gen_config.py

Generated files go to: src/generated/
"""

import yaml
import sys
from pathlib import Path
from typing import Dict, List, Any
from gen_preset_config import generate_preset_code

def main():
    """Main entry point"""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    params_file = project_root / "parameters.yaml"
    output_dir = project_root / "src" / "generated"

    if not params_file.exists():
        print(f"ERROR: {params_file} not found", file=sys.stderr)
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Reading {params_file}...")
    with open(params_file) as f:
        schema = yaml.safe_load(f)

    params = schema['parameters']
    print(f"Found {len(params)} parameters")

    # Check for iambic presets
    presets_schema = schema.get('iambic_presets')
    if presets_schema:
        print(f"Found iambic_presets with {presets_schema['count']} slots")

    print("Generating config.rs...")
    generate_config_rs(params, output_dir, presets_schema)

    print("Generating config_meta.rs...")
    generate_config_meta_rs(params, output_dir)

    print("Generating config_nvs.rs...")
    generate_config_nvs_rs(params, output_dir)

    print("Generating config_console.rs...")
    generate_config_console_rs(params, output_dir)

    print(f"✓ Code generation complete: {output_dir}")

def generate_config_rs(params: List[Dict], output_dir: Path, presets_schema: Dict = None):
    """Generate config.rs - Atomic configuration struct"""

    code = """// Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY
//
// This file defines the global atomic configuration structure.
// All configuration parameters are atomically accessible from any thread.

use core::sync::atomic::*;

/// Global keyer configuration with atomic access
///
/// All parameters use atomic types to allow lock-free access from
/// multiple threads (RT thread, UI thread, WiFi thread, etc.)
///
/// Change detection via `generation` counter:
/// - Increment `generation` when any parameter changes
/// - Consumers check `generation` to detect updates
pub struct KeyerConfig {
"""

    # Add fields
    for p in params:
        atomic_type = get_atomic_type(p)
        comment = get_field_comment(p)
        code += f"    /// {comment}\n"
        code += f"    pub {p['name']}: {atomic_type},\n\n"

    code += """    /// Configuration generation counter
    ///
    /// Incremented whenever any parameter changes.
    /// Consumers can detect config changes by comparing their cached generation.
    pub generation: AtomicU16,
}

impl KeyerConfig {
    /// Create new configuration with default values
    pub const fn new() -> Self {
        Self {
"""

    # Add initializers
    for p in params:
        default_val, comment = get_default_value(p)
        atomic_type = get_atomic_type(p)
        if comment:
            code += f"            {p['name']}: {atomic_type}::new({default_val}),  // {comment}\n"
        else:
            code += f"            {p['name']}: {atomic_type}::new({default_val}),\n"

    code += """            generation: AtomicU16::new(0),
        }
    }

    /// Increment generation counter to signal config change
    ///
    /// Call this after updating any parameter to notify consumers.
    pub fn bump_generation(&self) {
        self.generation.fetch_add(1, Ordering::Release);
    }
}

/// Static global configuration instance
pub static CONFIG: KeyerConfig = KeyerConfig::new();
"""

    # Add iambic presets if schema provided
    if presets_schema:
        code += generate_preset_code(presets_schema)

    with open(output_dir / "config.rs", "w") as f:
        f.write(code)

def generate_config_meta_rs(params: List[Dict], output_dir: Path):
    """Generate config_meta.rs - GUI metadata"""

    code = """// Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY
//
// This file defines GUI metadata for all configuration parameters.
// Includes labels, descriptions, widget types, and validation rules.

extern crate alloc;
use alloc::vec::Vec;

/// Parameter metadata for GUI generation
#[derive(Debug, Clone)]
pub struct ParamMeta {
    pub name: &'static str,
    pub label_short_en: &'static str,
    pub label_short_it: &'static str,
    pub label_long_en: &'static str,
    pub label_long_it: &'static str,
    pub description_en: &'static str,
    pub description_it: &'static str,
    pub category: &'static str,
    pub priority: u8,
    pub advanced: bool,
    pub widget: WidgetType,
    pub runtime_change: RuntimeChangeMode,
}

/// Widget type for parameter UI
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WidgetType {
    Slider,
    Spinbox,
    Dropdown,
    Toggle,
    Textbox,
}

/// When parameter changes can be applied
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RuntimeChangeMode {
    /// Applied immediately when changed
    Immediate,
    /// Applied only when paddles idle (preserves CW timing)
    IdleOnly,
    /// Requires reboot to apply
    Reboot,
}

/// All parameter metadata, sorted by priority
pub const PARAM_METADATA: &[ParamMeta] = &[
"""

    # Sort by priority
    sorted_params = sorted(params, key=lambda x: x['priority'])

    for p in sorted_params:
        gui = p['gui']

        # Extract labels
        label_short_en = gui['label_short']['en']
        label_short_it = gui['label_short']['it']
        label_long_en = gui['label_long']['en']
        label_long_it = gui['label_long']['it']
        desc_en = gui['description']['en']
        desc_it = gui['description']['it']

        widget = gui['widget'].capitalize()
        runtime_change = p['runtime_change'].replace('_', ' ').title().replace(' ', '')

        code += f"""    ParamMeta {{
        name: "{p['name']}",
        label_short_en: "{label_short_en}",
        label_short_it: "{label_short_it}",
        label_long_en: "{label_long_en}",
        label_long_it: "{label_long_it}",
        description_en: "{desc_en}",
        description_it: "{desc_it}",
        category: "{p['category']}",
        priority: {p['priority']},
        advanced: {str(p['gui']['advanced']).lower()},
        widget: WidgetType::{widget},
        runtime_change: RuntimeChangeMode::{runtime_change},
    }},
"""

    code += """];

/// Get metadata for a parameter by name
pub fn get_param_meta(name: &str) -> Option<&'static ParamMeta> {
    PARAM_METADATA.iter().find(|m| m.name == name)
}

/// Get all parameters in a category
pub fn get_params_by_category(category: &str) -> Vec<&'static ParamMeta> {
    PARAM_METADATA.iter()
        .filter(|m| m.category == category)
        .collect()
}

/// Get all non-advanced parameters (for simple UI)
pub fn get_basic_params() -> Vec<&'static ParamMeta> {
    PARAM_METADATA.iter()
        .filter(|m| !m.advanced)
        .collect()
}

/// Get all advanced parameters
pub fn get_advanced_params() -> Vec<&'static ParamMeta> {
    PARAM_METADATA.iter()
        .filter(|m| m.advanced)
        .collect()
}
"""

    with open(output_dir / "config_meta.rs", "w") as f:
        f.write(code)

def generate_config_nvs_rs(params: List[Dict], output_dir: Path):
    """Generate config_nvs.rs - NVS persistence"""

    code = """// Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY
//
// This file defines NVS (Non-Volatile Storage) persistence for configuration.
// Load/save functions for ESP32 NVS flash storage.

// Note: CONFIG will be accessed via crate::config::CONFIG when implemented
#![allow(unused_imports)]
use core::sync::atomic::Ordering;

/// NVS namespace for keyer configuration
pub const NVS_NAMESPACE: &str = "keyer_cfg";

/// Load all parameters from NVS
///
/// Reads configuration from NVS flash and updates CONFIG atomic values.
/// Falls back to defaults if NVS is empty or corrupted.
///
/// Returns: number of parameters successfully loaded
pub fn load_from_nvs() -> Result<usize, NvsError> {
    // TODO: Implement with esp-idf-svc NVS API
    // Placeholder for now
    Ok(0)
}

/// Save all parameters to NVS
///
/// Writes current CONFIG values to NVS flash.
/// Performs atomic commit after all values written.
///
/// Returns: number of parameters successfully saved
pub fn save_to_nvs() -> Result<usize, NvsError> {
    // TODO: Implement with esp-idf-svc NVS API
    // Placeholder for now
    Ok(0)
}

/// Load single parameter from NVS by name
pub fn load_param(name: &str) -> Result<(), NvsError> {
    match name {
"""

    for p in params:
        nvs_key = p['nvs_key']
        param_name = p['name']
        code += f"""        "{param_name}" => {{
            // TODO: nvs.get_{get_nvs_type(p)}("{nvs_key}")?
            // CONFIG.{param_name}.store(value, Ordering::Relaxed);
            Ok(())
        }}
"""

    code += """        _ => Err(NvsError::UnknownParameter),
    }
}

/// Save single parameter to NVS by name
pub fn save_param(name: &str) -> Result<(), NvsError> {
    match name {
"""

    for p in params:
        nvs_key = p['nvs_key']
        param_name = p['name']
        code += f"""        "{param_name}" => {{
            // let value = CONFIG.{param_name}.load(Ordering::Relaxed);
            // TODO: nvs.set_{get_nvs_type(p)}("{nvs_key}", value)?;
            Ok(())
        }}
"""

    code += """        _ => Err(NvsError::UnknownParameter),
    }
}

/// NVS operation errors
#[derive(Debug)]
pub enum NvsError {
    /// NVS initialization failed
    InitFailed,
    /// Parameter not found in schema
    UnknownParameter,
    /// NVS read/write error
    IoError,
    /// Value out of range
    ValidationError,
}

/// NVS key-value mapping for reference
pub mod nvs_keys {
"""

    for p in params:
        code += f"""    pub const {p['name'].upper()}: &str = "{p['nvs_key']}";\n"""

    code += """}
"""

    with open(output_dir / "config_nvs.rs", "w") as f:
        f.write(code)

def get_atomic_type(param: Dict) -> str:
    """Map parameter type to Rust atomic type"""
    type_map = {
        'u8': 'AtomicU8',
        'u16': 'AtomicU16',
        'u32': 'AtomicU32',
        'bool': 'AtomicBool',
        'enum': 'AtomicU8',  # Enums stored as u8
    }
    return type_map.get(param['type'], 'AtomicU32')

def get_nvs_type(param: Dict) -> str:
    """Map parameter type to NVS get/set suffix"""
    type_map = {
        'u8': 'u8',
        'u16': 'u16',
        'u32': 'u32',
        'bool': 'u8',  # Store bool as u8
        'enum': 'u8',
    }
    return type_map.get(param['type'], 'u32')

def get_default_value(param: Dict) -> tuple:
    """Get default value as Rust literal and optional comment.

    Returns: (value_str, comment_str or None)
    """
    if param['type'] == 'bool':
        return (str(param['default']).lower(), None)
    elif param['type'] == 'enum':
        # Enums: return index and enum name as comment
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
        range_str = f" (range: {param['range'][0]}-{param['range'][1]})"
    return f"{label}{range_str}"


def generate_config_console_rs(params: List[Dict], output_dir: Path):
    """Generate config_console.rs - Console parameter registry"""

    # Extract unique categories
    categories = sorted(set(p['category'] for p in params))

    code = """// Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY
//
// Console command parameter registry for set/show commands.

use core::sync::atomic::Ordering;
use super::config::CONFIG;

/// Parameter value union for get/set operations
#[derive(Debug, Clone, Copy)]
pub enum ParamValue {
    U8(u8),
    U16(u16),
    U32(u32),
    Bool(bool),
}

impl ParamValue {
    pub fn as_u8(&self) -> Option<u8> {
        match self { ParamValue::U8(v) => Some(*v), _ => None }
    }
    pub fn as_u16(&self) -> Option<u16> {
        match self { ParamValue::U16(v) => Some(*v), _ => None }
    }
    pub fn as_u32(&self) -> Option<u32> {
        match self { ParamValue::U32(v) => Some(*v), _ => None }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self { ParamValue::Bool(v) => Some(*v), _ => None }
    }
}

/// Parameter type with validation info
#[derive(Debug, Clone, Copy)]
pub enum ParamType {
    U8 { min: u8, max: u8 },
    U16 { min: u16, max: u16 },
    U32 { min: u32, max: u32 },
    Bool,
    Enum { max: u8 },
}

/// Parameter set error (used by generated set_fn)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParamSetError {
    /// Invalid value type for this parameter
    InvalidValue,
}

/// Single parameter descriptor
pub struct ParamDescriptor {
    pub name: &'static str,
    pub category: &'static str,
    pub param_type: ParamType,
    pub get_fn: fn() -> ParamValue,
    pub set_fn: fn(ParamValue) -> Result<(), ParamSetError>,
}

"""

    # Generate parameter descriptors
    code += "/// All parameters for console access\n"
    code += "pub static PARAMS: &[ParamDescriptor] = &[\n"

    for p in params:
        name = p['name']
        category = p['category']
        ptype = p['type']

        # Determine ParamType
        if ptype == 'bool':
            param_type = "ParamType::Bool"
        elif ptype == 'enum':
            max_val = len(p['enum_values']) - 1
            param_type = f"ParamType::Enum {{ max: {max_val} }}"
        else:
            min_val, max_val = p['range']
            param_type = f"ParamType::{ptype.upper()} {{ min: {min_val}, max: {max_val} }}"

        # Determine atomic type for get/set
        if ptype == 'u8' or ptype == 'enum':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U8"
            set_variant = "as_u8"
        elif ptype == 'u16':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U16"
            set_variant = "as_u16"
        elif ptype == 'u32':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U32"
            set_variant = "as_u32"
        elif ptype == 'bool':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "Bool"
            set_variant = "as_bool"
        else:
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U32"
            set_variant = "as_u32"

        code += f"""    ParamDescriptor {{
        name: "{name}",
        category: "{category}",
        param_type: {param_type},
        get_fn: || ParamValue::{get_variant}({atomic_load}),
        set_fn: |v| {{
            if let Some(val) = v.{set_variant}() {{
                CONFIG.{name}.store(val, Ordering::Relaxed);
                CONFIG.bump_generation();
                Ok(())
            }} else {{
                Err(ParamSetError::InvalidValue)
            }}
        }},
    }},
"""

    code += "];\n\n"

    # Generate categories array
    code += "/// All parameter categories\n"
    code += "pub static CATEGORIES: &[&str] = &[\n"
    for cat in categories:
        code += f'    "{cat}",\n'
    code += "];\n\n"

    # Helper functions
    code += """/// Find parameter by name
pub fn find_param(name: &str) -> Option<&'static ParamDescriptor> {
    PARAMS.iter().find(|p| p.name == name)
}

/// Find parameters matching pattern (prefix match on name or category)
pub fn find_params_matching(pattern: &str) -> impl Iterator<Item = &'static ParamDescriptor> {
    let pattern = pattern.trim_end_matches('*');
    PARAMS.iter().filter(move |p| {
        p.name.starts_with(pattern) || p.category.starts_with(pattern)
    })
}

/// Get all parameter names for tab completion
pub fn param_names() -> impl Iterator<Item = &'static str> {
    PARAMS.iter().map(|p| p.name)
}
"""

    with open(output_dir / "config_console.rs", "w") as f:
        f.write(code)


if __name__ == '__main__':
    main()
