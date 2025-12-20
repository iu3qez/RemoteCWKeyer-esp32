//! NVS persistence for iambic presets with schema versioning.
//!
//! Implements versioned schema with automatic migration for forward compatibility.
//!
//! # Version History
//!
//! - **v1** (current): Initial implementation with speed_wpm, iambic_mode, memory_mode,
//!   squeeze_mode, mem_block_start_pct, mem_block_end_pct
//!
//! # Future Migration Example
//!
//! When adding fields in v2:
//! 1. Increment CURRENT_SCHEMA_VERSION to 2
//! 2. Implement `migrate_v1_to_v2()` function
//! 3. Add new fields to `load_v2_presets()` and `save_v2_presets()`
//! 4. Update migration router in `migrate_presets()`

use crate::config::IAMBIC_PRESETS;
use core::cmp::Ordering;
use core::sync::atomic::Ordering as AtomicOrdering;

#[cfg(target_os = "espidf")]
use esp_idf_svc::nvs::*;
#[cfg(target_os = "espidf")]
use esp_idf_svc::sys::EspError;

#[cfg(target_os = "espidf")]
extern crate alloc;
#[cfg(target_os = "espidf")]
use alloc::format;

/// Current NVS schema version for iambic presets
pub const CURRENT_SCHEMA_VERSION: u32 = 1;

/// NVS namespace for iambic configuration
pub const NVS_NAMESPACE: &str = "iambic_cfg";

/// NVS key for schema version
const VERSION_KEY: &str = "schema_ver";

/// NVS key for active preset index
const ACTIVE_INDEX_KEY: &str = "active_idx";

/// Migration result
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MigrationResult {
    /// Fresh install, no migration needed (using defaults)
    FreshInstall,
    /// Schema up-to-date, loaded successfully
    UpToDate,
    /// Migrated from older version
    Migrated { from_version: u32, to_version: u32 },
}

/// NVS operation errors
#[derive(Debug)]
pub enum NvsError {
    /// NVS initialization failed
    #[cfg(target_os = "espidf")]
    InitFailed(EspError),
    /// Schema version too new (downgrade not supported)
    TooNew { stored_version: u32 },
    /// NVS read/write error
    #[cfg(target_os = "espidf")]
    IoError(EspError),
    /// Unsupported migration path
    UnsupportedMigration { from: u32, to: u32 },
    /// Feature not available on this platform
    #[cfg(not(target_os = "espidf"))]
    NotAvailable,
}

#[cfg(target_os = "espidf")]
impl From<EspError> for NvsError {
    fn from(e: EspError) -> Self {
        NvsError::IoError(e)
    }
}

/// Load presets from NVS with automatic migration
///
/// # Returns
///
/// - `Ok(MigrationResult)`: Load successful (may have migrated)
/// - `Err(NvsError::TooNew)`: Schema version too new (downgrade not supported)
/// - `Err(NvsError)`: Other NVS errors
///
/// # Example
///
/// ```no_run
/// use rust_remote_cw_keyer::config::nvs::{load_presets_from_nvs, MigrationResult};
///
/// match load_presets_from_nvs() {
///     Ok(MigrationResult::FreshInstall) => {
///         println!("Fresh install - using defaults");
///     }
///     Ok(MigrationResult::UpToDate) => {
///         println!("Presets loaded from NVS");
///     }
///     Ok(MigrationResult::Migrated { from_version, to_version }) => {
///         println!("Migrated: v{} → v{}", from_version, to_version);
///     }
///     Err(e) => {
///         eprintln!("NVS load failed: {:?}, using defaults", e);
///     }
/// }
/// ```
#[cfg(target_os = "espidf")]
pub fn load_presets_from_nvs() -> Result<MigrationResult, NvsError> {
    let nvs_default = EspDefaultNvsPartition::take()
        .map_err(NvsError::InitFailed)?;
    let mut storage = EspNvs::new(nvs_default, NVS_NAMESPACE, true)
        .map_err(NvsError::InitFailed)?;

    // Check schema version
    let stored_version = storage
        .get_u32(VERSION_KEY)?
        .unwrap_or(0);

    match stored_version.cmp(&CURRENT_SCHEMA_VERSION) {
        Ordering::Equal => {
            // Same version - direct load
            load_v1_presets(&storage)?;
            Ok(MigrationResult::UpToDate)
        }
        Ordering::Less => {
            if stored_version == 0 {
                // Fresh install - keep defaults
                Ok(MigrationResult::FreshInstall)
            } else {
                // Migrate from old version
                migrate_presets(&mut storage, stored_version, CURRENT_SCHEMA_VERSION)?;
                Ok(MigrationResult::Migrated {
                    from_version: stored_version,
                    to_version: CURRENT_SCHEMA_VERSION,
                })
            }
        }
        Ordering::Greater => {
            // Newer version - cannot downgrade
            Err(NvsError::TooNew { stored_version })
        }
    }
}

/// Stub for non-ESP platforms
#[cfg(not(target_os = "espidf"))]
pub fn load_presets_from_nvs() -> Result<MigrationResult, NvsError> {
    Err(NvsError::NotAvailable)
}

/// Save presets to NVS with version stamp
///
/// Writes all presets and active index to NVS flash with current schema version.
///
/// # Returns
///
/// - `Ok(())`: Save successful
/// - `Err(NvsError)`: NVS errors
#[cfg(target_os = "espidf")]
pub fn save_presets_to_nvs() -> Result<(), NvsError> {
    let nvs_default = EspDefaultNvsPartition::take()
        .map_err(NvsError::InitFailed)?;
    let mut storage = EspNvs::new(nvs_default, NVS_NAMESPACE, true)
        .map_err(NvsError::InitFailed)?;

    // Write version first
    storage.set_u32(VERSION_KEY, CURRENT_SCHEMA_VERSION)?;

    // Save active index
    let active = IAMBIC_PRESETS.active_index.load(AtomicOrdering::Relaxed);
    storage.set_u32(ACTIVE_INDEX_KEY, active)?;

    // Save all presets (v1 schema)
    save_v1_presets(&mut storage)?;

    Ok(())
}

/// Stub for non-ESP platforms
#[cfg(not(target_os = "espidf"))]
pub fn save_presets_to_nvs() -> Result<(), NvsError> {
    Err(NvsError::NotAvailable)
}

// ========================================
// v1 Schema Load/Save
// ========================================

#[cfg(target_os = "espidf")]
fn load_v1_presets(storage: &EspNvs<NvsDefault>) -> Result<(), NvsError> {
    // Load active index
    if let Some(active) = storage.get_u32(ACTIVE_INDEX_KEY)? {
        IAMBIC_PRESETS.activate(active);
    }

    // Load each preset
    for i in 0..10 {
        let preset = &IAMBIC_PRESETS.presets[i];

        // NVS keys for this preset
        let key_wpm = format!("p{}_wpm", i);
        let key_mode = format!("p{}_mode", i);
        let key_mem = format!("p{}_mem", i);
        let key_sqz = format!("p{}_sqz", i);
        let key_u = format!("p{}_u", i);
        let key_d = format!("p{}_d", i);

        // Load name (TODO: implement when AtomicString is ready)
        // let key_name = format!("p{}_name", i);
        // if let Some(name) = storage.get_str(&key_name, &mut [0u8; 32])? { ... }

        // Load parameters
        if let Some(wpm) = storage.get_u32(&key_wpm)? {
            preset.speed_wpm.store(wpm, AtomicOrdering::Relaxed);
        }
        if let Some(mode) = storage.get_u8(&key_mode)? {
            preset.iambic_mode.store(mode, AtomicOrdering::Relaxed);
        }
        if let Some(mem) = storage.get_u8(&key_mem)? {
            preset.memory_mode.store(mem, AtomicOrdering::Relaxed);
        }
        if let Some(sqz) = storage.get_u8(&key_sqz)? {
            preset.squeeze_mode.store(sqz, AtomicOrdering::Relaxed);
        }
        if let Some(u) = storage.get_u32(&key_u)? {
            preset.mem_block_start_pct.store(u, AtomicOrdering::Relaxed);
        }
        if let Some(d) = storage.get_u32(&key_d)? {
            preset.mem_block_end_pct.store(d, AtomicOrdering::Relaxed);
        }
    }

    Ok(())
}

#[cfg(target_os = "espidf")]
fn save_v1_presets(storage: &mut EspNvs<NvsDefault>) -> Result<(), NvsError> {
    for i in 0..10 {
        let preset = &IAMBIC_PRESETS.presets[i];

        // NVS keys for this preset
        let key_wpm = format!("p{}_wpm", i);
        let key_mode = format!("p{}_mode", i);
        let key_mem = format!("p{}_mem", i);
        let key_sqz = format!("p{}_sqz", i);
        let key_u = format!("p{}_u", i);
        let key_d = format!("p{}_d", i);

        // Save name (TODO: implement when AtomicString is ready)
        // let key_name = format!("p{}_name", i);
        // let name = preset.name.get();
        // if !name.is_empty() { storage.set_str(&key_name, name)?; }

        // Save parameters
        storage.set_u32(&key_wpm, preset.speed_wpm.load(AtomicOrdering::Relaxed))?;
        storage.set_u8(&key_mode, preset.iambic_mode.load(AtomicOrdering::Relaxed))?;
        storage.set_u8(&key_mem, preset.memory_mode.load(AtomicOrdering::Relaxed))?;
        storage.set_u8(&key_sqz, preset.squeeze_mode.load(AtomicOrdering::Relaxed))?;
        storage.set_u32(&key_u, preset.mem_block_start_pct.load(AtomicOrdering::Relaxed))?;
        storage.set_u32(&key_d, preset.mem_block_end_pct.load(AtomicOrdering::Relaxed))?;
    }

    Ok(())
}

// ========================================
// Migration Logic
// ========================================

#[cfg(target_os = "espidf")]
fn migrate_presets(
    storage: &mut EspNvs<NvsDefault>,
    from_version: u32,
    to_version: u32,
) -> Result<(), NvsError> {
    // Router for migration paths
    match (from_version, to_version) {
        // Future migrations will go here:
        //
        // (1, 2) => migrate_v1_to_v2(storage)?,
        // (2, 3) => migrate_v2_to_v3(storage)?,
        //
        // Chain migrations for multi-version jumps:
        // (1, 3) => {
        //     migrate_v1_to_v2(storage)?;
        //     migrate_v2_to_v3(storage)?;
        // }

        _ => {
            return Err(NvsError::UnsupportedMigration {
                from: from_version,
                to: to_version,
            });
        }
    }

    // Update version stamp
    storage.set_u32(VERSION_KEY, to_version)?;

    Ok(())
}

// ========================================
// Future Migration Functions (Examples)
// ========================================

// /// Migrate from v1 to v2 (example: adds sidetone_freq field)
// #[cfg(target_os = "espidf")]
// fn migrate_v1_to_v2(storage: &mut EspNvs) -> Result<(), NvsError> {
//     // Load v1 data
//     load_v1_presets(storage)?;
//
//     // Add v2 fields with defaults
//     for i in 0..10 {
//         let key_tone = format!("p{}_tone", i);
//         storage.set_u32(&key_tone, 700)?;  // 700Hz default
//     }
//
//     Ok(())
// }

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_schema_version_constant() {
        assert_eq!(CURRENT_SCHEMA_VERSION, 1);
    }

    #[test]
    fn test_migration_result_equality() {
        assert_eq!(MigrationResult::FreshInstall, MigrationResult::FreshInstall);
        assert_ne!(MigrationResult::FreshInstall, MigrationResult::UpToDate);

        let m1 = MigrationResult::Migrated {
            from_version: 1,
            to_version: 2,
        };
        let m2 = MigrationResult::Migrated {
            from_version: 1,
            to_version: 2,
        };
        assert_eq!(m1, m2);
    }

    #[test]
    fn test_nvs_namespace_constant() {
        assert_eq!(NVS_NAMESPACE, "iambic_cfg");
    }
}
