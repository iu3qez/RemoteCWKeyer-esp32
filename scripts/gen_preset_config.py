#!/usr/bin/env python3
"""
RustRemoteCWKeyer - Preset Configuration Code Generator

Generates code for iambic_presets section of parameters.yaml:
- Enum types (IambicMode, MemoryMode, SqueezeMode)
- IambicPreset struct
- IambicPresets container
- Static IAMBIC_PRESETS instance

This is called by gen_config.py as part of the build process.
"""

from pathlib import Path
from typing import Dict, Any


def generate_preset_code(presets_schema: Dict[str, Any]) -> str:
    """Generate Rust code for iambic presets system

    Args:
        presets_schema: The 'iambic_presets' section from parameters.yaml

    Returns:
        Rust code string to be included in config.rs
    """

    preset_count = presets_schema['count']
    template = presets_schema['preset_template']

    code = f"""
// ========================================
// Iambic Preset System (Auto-generated)
// ========================================

/// Iambic keying mode
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IambicMode {{
    ModeA = 0,
    ModeB = 1,
}}

/// Memory mode for paddle state
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemoryMode {{
    None = 0,
    DotOnly = 1,
    DahOnly = 2,
    DotAndDah = 3,
}}

/// Squeeze timing mode
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SqueezeMode {{
    LatchOff = 0,  // Live/immediate response
    LatchOn = 1,   // Snapshot/latch at element start
}}

/// Atomic string wrapper for preset names
pub struct AtomicString<const N: usize> {{
    // For now, use AtomicU32 array to store string
    // TODO: Implement proper atomic string when needed
    _marker: core::marker::PhantomData<[u8; N]>,
}}

impl<const N: usize> AtomicString<N> {{
    pub const fn new() -> Self {{
        Self {{
            _marker: core::marker::PhantomData,
        }}
    }}

    pub fn get(&self) -> &str {{
        // Placeholder - return empty string for now
        ""
    }}

    pub fn set(&self, _s: &str) {{
        // Placeholder - will implement with proper atomic string later
    }}
}}

/// Single iambic preset configuration
pub struct IambicPreset {{
    pub name: AtomicString<32>,
    pub speed_wpm: AtomicU32,
    pub iambic_mode: AtomicU8,
    pub memory_mode: AtomicU8,
    pub squeeze_mode: AtomicU8,
    pub mem_block_start_pct: AtomicU32,  // f32 as bits
    pub mem_block_end_pct: AtomicU32,    // f32 as bits
}}

impl IambicPreset {{
    /// Create new preset with default values
    pub const fn new() -> Self {{
        Self {{
            name: AtomicString::new(),
            speed_wpm: AtomicU32::new({template['speed_wpm']['default']}),
            iambic_mode: AtomicU8::new({get_enum_default_index(template['iambic_mode'])}),  // {template['iambic_mode']['default']}
            memory_mode: AtomicU8::new({get_enum_default_index(template['memory_mode'])}),  // {template['memory_mode']['default']}
            squeeze_mode: AtomicU8::new({get_enum_default_index(template['squeeze_mode'])}),  // {template['squeeze_mode']['default']}
            mem_block_start_pct: AtomicU32::new({float_to_bits(template['mem_block_start_pct']['default'])}),  // {template['mem_block_start_pct']['default']}%
            mem_block_end_pct: AtomicU32::new({float_to_bits(template['mem_block_end_pct']['default'])}),  // {template['mem_block_end_pct']['default']}%
        }}
    }}

    /// Get speed in WPM
    #[inline]
    pub fn get_speed_wpm(&self) -> u32 {{
        self.speed_wpm.load(Ordering::Relaxed)
    }}

    /// Get memory window start percentage
    #[inline]
    pub fn get_mem_start(&self) -> f32 {{
        f32::from_bits(self.mem_block_start_pct.load(Ordering::Relaxed))
    }}

    /// Get memory window end percentage
    #[inline]
    pub fn get_mem_end(&self) -> f32 {{
        f32::from_bits(self.mem_block_end_pct.load(Ordering::Relaxed))
    }}

    /// Get iambic mode
    #[inline]
    pub fn get_iambic_mode(&self) -> IambicMode {{
        unsafe {{ core::mem::transmute(self.iambic_mode.load(Ordering::Relaxed)) }}
    }}

    /// Get memory mode
    #[inline]
    pub fn get_memory_mode(&self) -> MemoryMode {{
        unsafe {{ core::mem::transmute(self.memory_mode.load(Ordering::Relaxed)) }}
    }}

    /// Get squeeze mode
    #[inline]
    pub fn get_squeeze_mode(&self) -> SqueezeMode {{
        unsafe {{ core::mem::transmute(self.squeeze_mode.load(Ordering::Relaxed)) }}
    }}
}}

/// Container for all iambic presets
pub struct IambicPresets {{
    pub presets: [IambicPreset; {preset_count}],
    pub active_index: AtomicU32,
}}

impl IambicPresets {{
    /// Create new presets container with defaults
    pub const fn new() -> Self {{
        Self {{
            presets: [
"""

    # Generate array initialization (10 presets)
    for i in range(preset_count):
        code += "                IambicPreset::new(),\n"

    code += f"""            ],
            active_index: AtomicU32::new(0),
        }}
    }}

    /// Get currently active preset (RT-safe)
    #[inline]
    pub fn active(&self) -> &IambicPreset {{
        let idx = self.active_index.load(Ordering::Relaxed) as usize;
        &self.presets[idx.min({preset_count - 1})]
    }}

    /// Switch to preset by index
    pub fn activate(&self, index: u32) {{
        if index < {preset_count} {{
            self.active_index.store(index, Ordering::Relaxed);
        }}
    }}

    /// Get preset by index
    pub fn get(&self, index: usize) -> Option<&IambicPreset> {{
        if index < {preset_count} {{
            Some(&self.presets[index])
        }} else {{
            None
        }}
    }}
}}

/// Static global iambic presets instance
pub static IAMBIC_PRESETS: IambicPresets = IambicPresets::new();
"""

    return code


def get_enum_default_index(enum_param: Dict[str, Any]) -> int:
    """Get the index of the default enum value"""
    enum_values = enum_param['enum_values']
    default_value = enum_param['default']
    return enum_values.index(default_value)


def float_to_bits(f: float) -> str:
    """Convert float to u32 bits representation for Rust const"""
    import struct
    bits = struct.unpack('I', struct.pack('f', f))[0]
    return f"0x{bits:08X}"


def main():
    """Test the generator standalone"""
    import yaml

    # Load parameters.yaml
    params_file = Path(__file__).parent.parent / "parameters.yaml"
    with open(params_file) as f:
        schema = yaml.safe_load(f)

    if 'iambic_presets' in schema:
        code = generate_preset_code(schema['iambic_presets'])
        print(code)
    else:
        print("No iambic_presets section found in parameters.yaml")


if __name__ == '__main__':
    main()
