//! Module: config
//!
//! Purpose: Configuration system for RustRemoteCWKeyer.
//! Exposes auto-generated code from parameters.yaml.
//!
//! Architecture:
//! - parameters.yaml: Single source of truth for all config
//! - scripts/gen_config.py: Code generation at build time
//! - Generated code: config.rs, config_meta.rs, config_nvs.rs
//! - All config atomically accessible (lock-free)
//!
//! Safety: RT-safe. All access via atomics, no locks.

// Include auto-generated code
#[path = "../generated/config.rs"]
mod generated_config;

#[path = "../generated/config_meta.rs"]
mod generated_meta;

#[path = "../generated/config_nvs.rs"]
mod generated_nvs;

#[path = "../generated/config_console.rs"]
mod generated_console;

// NVS persistence for iambic presets
pub mod nvs;

// Re-export public API
pub use generated_config::{KeyerConfig, CONFIG, IAMBIC_PRESETS, IambicPreset, IambicMode, MemoryMode, SqueezeMode};
pub use generated_meta::{
    ParamMeta, WidgetType, RuntimeChangeMode,
    PARAM_METADATA,
    get_param_meta, get_params_by_category,
    get_basic_params, get_advanced_params,
};
pub use generated_nvs::{
    load_from_nvs, save_to_nvs,
    load_param, save_param,
    NvsError, nvs_keys,
    NVS_NAMESPACE,
};
pub use generated_console::{
    ParamValue, ParamType, ParamSetError,
    ParamDescriptor, PARAMS, CATEGORIES,
    find_param, find_params_matching, param_names,
};
