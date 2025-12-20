# Iambic Keyer Preset System Design

**Date**: 2025-01-19
**Status**: Approved for implementation
**Author**: Claude (Brainstorming with user)

## Overview

Design and implementation of a professional-grade preset system for the iambic keyer, allowing users to save and recall 10 named configurations with different speeds, modes, and memory window settings.

## Motivation

Professional CW operators use different keying parameters for different scenarios:
- Contest vs casual operation (different speeds)
- Learning vs production (different memory modes)
- Personal preference variations (squeeze modes, memory windows)

The preset system allows instant switching between configurations without manual parameter adjustment.

## User Requirements

User stated: **"Uso sempre e solo modalità iambica completa"** (I only ever use full iambic mode)

This indicates:
- Expert-level iambic keyer user
- Needs fine-grained control over memory windows and squeeze behavior
- Values predictability and precision in keying

## Design Principles

Following ARCHITECTURE.md immutable principles:

1. **RT-Safe Access**: Preset reads must be <200ns, never block RT path
2. **Static Allocation**: No heap, all data compile-time sized
3. **Atomic Operations**: All configuration changes via atomics
4. **Testable**: Unit tests without hardware
5. **Persistent**: NVS storage survives reboots
6. **Versioned**: Schema evolution support for future changes

## Architecture

### Parameters Schema (parameters.yaml)

```yaml
# Preset system configuration
iambic_presets:
  schema_version: 1  # Metadata for migration tracking

  count:
    type: const
    value: 10
    description: "Number of preset slots (compile-time constant)"

  active_index:
    type: uint32_t
    default: 0
    min: 0
    max: 9
    label: "Active Preset"
    description: "Currently selected preset (0-9)"

# Template for a single preset (replicated 10x by codegen)
iambic_preset_template:
  name:
    type: string
    max_length: 32
    default: ""
    label: "Preset Name"
    description: "User-defined name (empty = unused slot)"

  speed_wpm:
    type: uint32_t
    default: 25
    min: 5
    max: 100
    unit: "WPM"
    label: "Keying Speed"
    description: "Words per minute (PARIS standard: 50 dit units)"

  iambic_mode:
    type: enum
    default: MODE_B
    values:
      MODE_A: "Mode A (no squeeze)"
      MODE_B: "Mode B (squeeze keying)"
    label: "Iambic Mode"
    description: "Mode A completes element, Mode B adds opposite"

  memory_mode:
    type: enum
    default: DOT_AND_DAH
    values:
      NONE: "No memory"
      DOT_ONLY: "Remember dot only"
      DAH_ONLY: "Remember dah only"
      DOT_AND_DAH: "Remember both paddles"
    label: "Memory Mode"
    description: "Which paddles remembered during element transmission"

  squeeze_mode:
    type: enum
    default: LATCH_ON
    values:
      LATCH_OFF: "Live (immediate response)"
      LATCH_ON: "Snapshot (latch at element start)"
    label: "Squeeze Timing"
    description: "When to sample paddle state for squeeze detection"

  mem_block_start_pct:
    type: float
    default: 60.0
    min: 0.0
    max: 100.0
    unit: "%"
    label: "Memory Window Start (U)"
    description: "Memory window start - inputs before are ignored"

  mem_block_end_pct:
    type: float
    default: 99.0
    min: 0.0
    max: 100.0
    unit: "%"
    label: "Memory Window End (D)"
    description: "Memory window end - inputs after are ignored"
```

### Memory Window Visualization

```
|<-------- Element Duration (48ms @ 25 WPM) -------->|
|                                                     |
0%             U=60%                      D=99%      100%
|---------------|---------------------------|---------|
|  NON memor.   |      MEMORIZZATO         | NON mem.|
|    (RED)      |        (GREEN)           |  (RED)  |
|---------------|---------------------------|---------|
```

**Behavior**:
- **0% → U%**: Paddle input **ignored** (too early, debounce/false trigger)
- **U% → D%**: Paddle input **remembered** (valid squeeze window)
- **D% → 100%**: Paddle input **ignored** (too late, next element)

### Generated Code Structure (config.rs)

```rust
// Enums for modes
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IambicMode {
    ModeA = 0,
    ModeB = 1,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemoryMode {
    None = 0,
    DotOnly = 1,
    DahOnly = 2,
    DotAndDah = 3,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SqueezeMode {
    LatchOff = 0,  // Live/immediate
    LatchOn = 1,   // Snapshot/latch
}

// Single preset structure
pub struct IambicPreset {
    pub name: AtomicString<32>,
    pub speed_wpm: AtomicU32,
    pub iambic_mode: AtomicU8,
    pub memory_mode: AtomicU8,
    pub squeeze_mode: AtomicU8,
    pub mem_block_start_pct: AtomicU32,  // f32 bits
    pub mem_block_end_pct: AtomicU32,    // f32 bits
}

impl IambicPreset {
    pub const fn new() -> Self {
        Self {
            name: AtomicString::new(),
            speed_wpm: AtomicU32::new(25),
            iambic_mode: AtomicU8::new(IambicMode::ModeB as u8),
            memory_mode: AtomicU8::new(MemoryMode::DotAndDah as u8),
            squeeze_mode: AtomicU8::new(SqueezeMode::LatchOn as u8),
            mem_block_start_pct: AtomicU32::new(f32::to_bits(60.0)),
            mem_block_end_pct: AtomicU32::new(f32::to_bits(99.0)),
        }
    }

    // Helper methods
    pub fn get_speed_wpm(&self) -> u32 {
        self.speed_wpm.load(Ordering::Relaxed)
    }

    pub fn get_mem_start(&self) -> f32 {
        f32::from_bits(self.mem_block_start_pct.load(Ordering::Relaxed))
    }

    pub fn get_mem_end(&self) -> f32 {
        f32::from_bits(self.mem_block_end_pct.load(Ordering::Relaxed))
    }
}

// Global presets array
pub struct IambicPresets {
    pub presets: [IambicPreset; 10],
    pub active_index: AtomicU32,
}

impl IambicPresets {
    pub const fn new() -> Self {
        Self {
            presets: [
                IambicPreset::new(), IambicPreset::new(),
                IambicPreset::new(), IambicPreset::new(),
                IambicPreset::new(), IambicPreset::new(),
                IambicPreset::new(), IambicPreset::new(),
                IambicPreset::new(), IambicPreset::new(),
            ],
            active_index: AtomicU32::new(0),
        }
    }

    /// Get currently active preset (RT-safe)
    #[inline]
    pub fn active(&self) -> &IambicPreset {
        let idx = self.active_index.load(Ordering::Relaxed) as usize;
        &self.presets[idx.min(9)]
    }

    /// Switch to preset by index
    pub fn activate(&self, index: u32) {
        if index < 10 {
            self.active_index.store(index, Ordering::Relaxed);
        }
    }
}

pub static IAMBIC_PRESETS: IambicPresets = IambicPresets::new();
```

### IambicProcessor Integration (src/iambic.rs)

```rust
use crate::config::{IAMBIC_PRESETS, IambicMode, MemoryMode, SqueezeMode};

pub struct IambicProcessor {
    state: IambicState,
    memory: PaddleMemory,
    element_start_us: i64,
    element_duration_us: i64,

    // Cached values from active preset
    dit_duration_us: i64,
    dah_duration_us: i64,
    space_duration_us: i64,
    mem_start_pct: f32,
    mem_end_pct: f32,
    iambic_mode: IambicMode,
    memory_mode: MemoryMode,
    squeeze_mode: SqueezeMode,
}

impl IambicProcessor {
    pub fn new() -> Self {
        let mut processor = Self {
            // ... init fields ...
        };
        processor.update_from_preset();
        processor
    }

    /// Update cached timing from active preset
    fn update_from_preset(&mut self) {
        let preset = IAMBIC_PRESETS.active();

        // Calculate timing from WPM
        // PARIS standard: 50 dit units per word
        let wpm = preset.get_speed_wpm();
        let dit_us = (60_000_000 / (50 * wpm)) as i64;

        self.dit_duration_us = dit_us;
        self.dah_duration_us = dit_us * 3;
        self.space_duration_us = dit_us;

        // Cache window percentages
        self.mem_start_pct = preset.get_mem_start();
        self.mem_end_pct = preset.get_mem_end();

        // Cache modes
        self.iambic_mode = unsafe {
            core::mem::transmute(preset.iambic_mode.load(Ordering::Relaxed))
        };
        self.memory_mode = unsafe {
            core::mem::transmute(preset.memory_mode.load(Ordering::Relaxed))
        };
        self.squeeze_mode = unsafe {
            core::mem::transmute(preset.squeeze_mode.load(Ordering::Relaxed))
        };
    }

    /// Process paddle inputs (RT path)
    pub fn process(&mut self, now_us: i64, gpio: &GpioState) -> bool {
        // Calculate element progress
        let element_progress_us = now_us - self.element_start_us;
        let progress_pct = if self.element_duration_us > 0 {
            (element_progress_us * 100) as f32 / self.element_duration_us as f32
        } else {
            0.0
        };

        // Check if in memory window (GREEN zone)
        let in_memory_window = progress_pct >= self.mem_start_pct
                            && progress_pct <= self.mem_end_pct;

        // ... FSM logic using memory window and modes ...
    }
}
```

### NVS Persistence with Versioning (config_nvs.rs)

```rust
/// NVS schema version
const CURRENT_SCHEMA_VERSION: u32 = 1;
const NVS_NAMESPACE: &str = "iambic_cfg";
const VERSION_KEY: &str = "schema_ver";

/// Migration result
#[derive(Debug)]
pub enum MigrationResult {
    FreshInstall,
    UpToDate,
    Migrated { from_version: u32, to_version: u32 },
    TooNew { stored_version: u32 },
}

/// Load with automatic migration
pub fn load_presets_from_nvs() -> Result<MigrationResult, EspError> {
    let nvs = EspDefaultNvsPartition::take()?;
    let mut storage = EspNvs::new(nvs, NVS_NAMESPACE, true)?;

    let stored_version = storage.get_u32(VERSION_KEY)?.unwrap_or(0);

    match stored_version.cmp(&CURRENT_SCHEMA_VERSION) {
        Ordering::Equal => {
            load_v1_presets(&storage)?;
            Ok(MigrationResult::UpToDate)
        }
        Ordering::Less => {
            if stored_version == 0 {
                Ok(MigrationResult::FreshInstall)
            } else {
                migrate_presets(&mut storage, stored_version, CURRENT_SCHEMA_VERSION)?;
                Ok(MigrationResult::Migrated {
                    from_version: stored_version,
                    to_version: CURRENT_SCHEMA_VERSION,
                })
            }
        }
        Ordering::Greater => {
            Err(EspError::from_infallible::<ESP_ERR_NOT_SUPPORTED>())
        }
    }
}

/// Save with version stamp
pub fn save_presets_to_nvs() -> Result<(), EspError> {
    let nvs = EspDefaultNvsPartition::take()?;
    let mut storage = EspNvs::new(nvs, NVS_NAMESPACE, true)?;

    storage.set_u32(VERSION_KEY, CURRENT_SCHEMA_VERSION)?;
    save_v1_presets(&mut storage)?;

    Ok(())
}
```

**NVS Key Schema (v1)**:
```
schema_ver: u32         -> Current schema version
active_idx: u32         -> Active preset index (0-9)

For each preset i (0-9):
  p{i}_name: str        -> Preset name (max 32 bytes)
  p{i}_wpm: u32         -> Speed in WPM
  p{i}_mode: u8         -> IambicMode enum
  p{i}_mem: u8          -> MemoryMode enum
  p{i}_sqz: u8          -> SqueezeMode enum
  p{i}_u: u32           -> mem_block_start_pct (f32 bits)
  p{i}_d: u32           -> mem_block_end_pct (f32 bits)
```

## Testing Strategy

### Unit Tests (no hardware)

```rust
#[test]
fn test_preset_timing_calculation() {
    // Verify WPM → microseconds conversion
    // 25 WPM: dit = 48ms = 48000us
}

#[test]
fn test_memory_window_boundaries() {
    // Verify U% and D% zone logic
}

#[test]
fn test_mode_a_no_squeeze() {
    // Mode A + no memory = ignores opposite paddle
}

#[test]
fn test_mode_b_squeeze() {
    // Mode B + full memory = remembers in window
}

#[test]
fn test_preset_switching() {
    // Switch between presets, verify timing changes
}

#[test]
fn test_nvs_migration_v1_fresh() {
    // Fresh install returns default presets
}
```

### Integration Tests (future)

- Load/save round-trip to real NVS
- Timing precision under real hardware constraints
- Memory window behavior with real paddle input

## Implementation Plan

1. ✅ Design complete (this document)
2. Update `parameters.yaml` with preset schema
3. Modify `scripts/gen_config.py` to generate preset structures
4. Update `src/iambic.rs` with preset integration
5. Create `src/config/nvs.rs` with versioned persistence
6. Add unit tests
7. Build and verify

## Migration Strategy

### Version History

**v1 (current)**:
- Initial implementation
- Fields: speed_wpm, iambic_mode, memory_mode, squeeze_mode, mem_block_start_pct, mem_block_end_pct

**v2 (future example)**:
- Add `sidetone_freq` per preset
- Migration: Add default 700Hz to all presets

**v3 (future example)**:
- Add `paddle_reversed` per preset
- Migration: Add default `false` to all presets

### Migration Rules

1. **Forward-compatible only**: Can migrate v1→v2→v3, cannot downgrade
2. **Chainable**: v1→v3 runs v1→v2 then v2→v3 automatically
3. **Logged**: Migration results logged to RT_LOG_STREAM
4. **Safe**: Rejects too-new schemas with error

## Performance Characteristics

- **Preset read latency**: ~20ns (atomic load + array index)
- **Preset switch latency**: ~50ns (atomic store + cache reload)
- **NVS load time**: ~100ms (one-time at boot)
- **NVS save time**: ~200ms (on demand, Core 1)
- **Memory footprint**: ~1.5KB (10 presets × 152 bytes)

## Trade-offs and Decisions

### Why 10 presets?

- **YAGNI**: User requested "una decina" (about ten)
- **Simple**: No dynamic sizing complexity
- **Sufficient**: Professional ops rarely need >10 configs
- **Fast**: Array index, no indirection

### Why hybrid YAML approach?

- **DRY**: Template defined once, replicated by codegen
- **Maintainable**: Changes to preset schema in one place
- **Type-safe**: Generated Rust code verified at compile-time

### Why versioned NVS?

- **Future-proof**: Schema will evolve (sidetone, paddle reversal, etc.)
- **Robust**: Prevents corruption from version mismatch
- **Traceable**: Logs show migration history for debugging

## Compliance with ARCHITECTURE.md

- ✅ **RULE 4.1.1**: Static allocation only (no heap)
- ✅ **RULE 4.2.1**: Atomic operations only (no mutexes)
- ✅ **RULE 4.3.1**: RT path <100µs (preset access ~20ns)
- ✅ **RULE 7.1.1**: Stream-based tests (preset mocking)
- ✅ **RULE 11.1.3**: RT logging only (via rt_log! macros)

## Open Questions

None - design validated with user.

## References

- ARCHITECTURE.md §4 (Hard Real-Time Path)
- ARCHITECTURE.md §7 (Testing Principles)
- parameters.yaml (configuration schema)
- scripts/gen_config.py (code generator)
