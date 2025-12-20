//! Integration tests for iambic preset system
//!
//! Tests the complete preset system including:
//! - Preset creation and initialization
//! - Active preset selection
//! - Timing calculation from WPM
//! - Mode conversions
//! - Memory window boundaries

use rust_remote_cw_keyer::config::{IAMBIC_PRESETS, IambicMode, MemoryMode, SqueezeMode};
use core::sync::atomic::Ordering;

#[test]
fn test_preset_default_values() {
    // Verify default preset values match design spec
    let preset = IAMBIC_PRESETS.get(0).unwrap();

    assert_eq!(preset.get_speed_wpm(), 25, "Default WPM should be 25");
    assert_eq!(
        preset.get_iambic_mode(),
        IambicMode::ModeB,
        "Default should be Mode B"
    );
    assert_eq!(
        preset.get_memory_mode(),
        MemoryMode::DotAndDah,
        "Default should be full memory"
    );
    assert_eq!(
        preset.get_squeeze_mode(),
        SqueezeMode::LatchOn,
        "Default should be snapshot/latch"
    );

    // Memory window defaults
    assert_eq!(
        preset.get_mem_start(),
        60.0,
        "Default memory window start should be 60%"
    );
    assert_eq!(
        preset.get_mem_end(),
        99.0,
        "Default memory window end should be 99%"
    );
}

#[test]
fn test_preset_count() {
    // Verify we have exactly 10 preset slots
    assert_eq!(IAMBIC_PRESETS.presets.len(), 10, "Should have 10 preset slots");

    // All slots should be accessible
    for i in 0..10 {
        assert!(
            IAMBIC_PRESETS.get(i).is_some(),
            "Preset slot {} should exist",
            i
        );
    }

    // Out of bounds should return None
    assert!(IAMBIC_PRESETS.get(10).is_none(), "Slot 10 should not exist");
}

#[test]
fn test_active_preset_switching() {
    // Initially active preset 0
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        0,
        "Initial active index should be 0"
    );

    // Switch to preset 5
    IAMBIC_PRESETS.activate(5);
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        5,
        "Active index should be 5 after activation"
    );

    let active = IAMBIC_PRESETS.active();
    assert_eq!(
        active.get_speed_wpm(),
        25,
        "Active preset should have default values"
    );

    // Switch back to preset 0
    IAMBIC_PRESETS.activate(0);
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        0,
        "Should switch back to preset 0"
    );
}

#[test]
fn test_preset_bounds_checking() {
    // Activate valid preset
    IAMBIC_PRESETS.activate(9);
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        9,
        "Should activate preset 9"
    );

    // Try to activate out-of-bounds preset (should be ignored)
    IAMBIC_PRESETS.activate(10);
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        9,
        "Out-of-bounds activation should be ignored"
    );

    IAMBIC_PRESETS.activate(100);
    assert_eq!(
        IAMBIC_PRESETS.active_index.load(Ordering::Relaxed),
        9,
        "Large out-of-bounds should be ignored"
    );

    // Reset for other tests
    IAMBIC_PRESETS.activate(0);
}

#[test]
fn test_wpm_timing_calculation() {
    let preset = IAMBIC_PRESETS.get(0).unwrap();

    // Modify preset 0 to 25 WPM
    preset.speed_wpm.store(25, Ordering::Relaxed);
    assert_eq!(preset.get_speed_wpm(), 25);

    // PARIS standard: dit = 1.2s / WPM = 1,200,000 µs / WPM
    // At 25 WPM: dit = 48,000 µs = 48ms
    // Dah = 3x dit = 144,000 µs = 144ms

    // Note: Actual timing calculation is in IambicConfig::from_preset()
    // Here we just verify the WPM value is stored/retrieved correctly
}

#[test]
fn test_mode_enum_values() {
    // Verify enum discriminants match design
    assert_eq!(IambicMode::ModeA as u8, 0);
    assert_eq!(IambicMode::ModeB as u8, 1);

    assert_eq!(MemoryMode::None as u8, 0);
    assert_eq!(MemoryMode::DotOnly as u8, 1);
    assert_eq!(MemoryMode::DahOnly as u8, 2);
    assert_eq!(MemoryMode::DotAndDah as u8, 3);

    assert_eq!(SqueezeMode::LatchOff as u8, 0);
    assert_eq!(SqueezeMode::LatchOn as u8, 1);
}

#[test]
fn test_preset_modification() {
    let preset = IAMBIC_PRESETS.get(1).unwrap();

    // Modify preset 1 parameters
    preset.speed_wpm.store(40, Ordering::Relaxed);
    preset.iambic_mode.store(IambicMode::ModeA as u8, Ordering::Relaxed);
    preset
        .memory_mode
        .store(MemoryMode::DotOnly as u8, Ordering::Relaxed);
    preset
        .squeeze_mode
        .store(SqueezeMode::LatchOff as u8, Ordering::Relaxed);

    // Verify modifications
    assert_eq!(preset.get_speed_wpm(), 40);
    assert_eq!(preset.get_iambic_mode(), IambicMode::ModeA);
    assert_eq!(preset.get_memory_mode(), MemoryMode::DotOnly);
    assert_eq!(preset.get_squeeze_mode(), SqueezeMode::LatchOff);

    // Reset for other tests
    preset.speed_wpm.store(25, Ordering::Relaxed);
    preset
        .iambic_mode
        .store(IambicMode::ModeB as u8, Ordering::Relaxed);
    preset
        .memory_mode
        .store(MemoryMode::DotAndDah as u8, Ordering::Relaxed);
    preset
        .squeeze_mode
        .store(SqueezeMode::LatchOn as u8, Ordering::Relaxed);
}

#[test]
fn test_memory_window_float_conversion() {
    let preset = IAMBIC_PRESETS.get(2).unwrap();

    // Set memory window boundaries (as f32 bits)
    preset
        .mem_block_start_pct
        .store(50.0f32.to_bits(), Ordering::Relaxed);
    preset
        .mem_block_end_pct
        .store(95.0f32.to_bits(), Ordering::Relaxed);

    // Verify conversion back to float
    assert_eq!(preset.get_mem_start(), 50.0);
    assert_eq!(preset.get_mem_end(), 95.0);

    // Test edge cases
    preset
        .mem_block_start_pct
        .store(0.0f32.to_bits(), Ordering::Relaxed);
    preset
        .mem_block_end_pct
        .store(100.0f32.to_bits(), Ordering::Relaxed);

    assert_eq!(preset.get_mem_start(), 0.0);
    assert_eq!(preset.get_mem_end(), 100.0);

    // Reset to defaults
    preset
        .mem_block_start_pct
        .store(60.0f32.to_bits(), Ordering::Relaxed);
    preset
        .mem_block_end_pct
        .store(99.0f32.to_bits(), Ordering::Relaxed);
}

#[test]
fn test_preset_independence() {
    // Modify preset 3
    let preset3 = IAMBIC_PRESETS.get(3).unwrap();
    preset3.speed_wpm.store(35, Ordering::Relaxed);

    // Modify preset 7
    let preset7 = IAMBIC_PRESETS.get(7).unwrap();
    preset7.speed_wpm.store(15, Ordering::Relaxed);

    // Verify they're independent
    assert_eq!(preset3.get_speed_wpm(), 35);
    assert_eq!(preset7.get_speed_wpm(), 15);

    // Preset 4 should be unaffected
    let preset4 = IAMBIC_PRESETS.get(4).unwrap();
    assert_eq!(preset4.get_speed_wpm(), 25, "Unmodified preset should have default");

    // Reset
    preset3.speed_wpm.store(25, Ordering::Relaxed);
    preset7.speed_wpm.store(25, Ordering::Relaxed);
}

#[test]
fn test_concurrent_access_pattern() {
    // Simulate RT thread reading active preset while background thread modifies another
    IAMBIC_PRESETS.activate(0);
    let active = IAMBIC_PRESETS.active();
    let active_wpm = active.get_speed_wpm(); // RT thread read

    // Background thread modifies preset 8
    let preset8 = IAMBIC_PRESETS.get(8).unwrap();
    preset8.speed_wpm.store(50, Ordering::Relaxed);

    // RT thread's cached value should be unaffected
    assert_eq!(active_wpm, 25, "Active preset read should be stable");

    // Verify preset 8 was modified
    assert_eq!(preset8.get_speed_wpm(), 50);

    // Reset
    preset8.speed_wpm.store(25, Ordering::Relaxed);
}

#[test]
fn test_active_preset_bounds_safety() {
    // Set active index to max valid value
    IAMBIC_PRESETS.activate(9);

    // active() should clamp to valid range even if index corrupted
    let active = IAMBIC_PRESETS.active();
    assert_eq!(active.get_speed_wpm(), 25, "Should return valid preset");

    // Manually corrupt index (simulate race condition)
    IAMBIC_PRESETS.active_index.store(100, Ordering::Relaxed);

    // active() should still return safe value (clamped to 9)
    let active = IAMBIC_PRESETS.active();
    assert_eq!(
        active.get_speed_wpm(),
        25,
        "Corrupted index should be clamped"
    );

    // Reset
    IAMBIC_PRESETS.activate(0);
}
