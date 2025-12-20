//! Sine lookup table tests

use rust_remote_cw_keyer::audio::lut::{SINE_LUT, LUT_SIZE};

#[test]
fn test_lut_size() {
    assert_eq!(SINE_LUT.len(), LUT_SIZE);
    assert_eq!(LUT_SIZE, 256);
}

#[test]
fn test_lut_zero_crossing() {
    // Index 0 should be ~0 (sine starts at 0)
    assert!(SINE_LUT[0].abs() < 100, "LUT[0] should be near zero");

    // Index 64 should be ~max (sine peak at 90°)
    assert!(SINE_LUT[64] > 30000, "LUT[64] should be near max");

    // Index 128 should be ~0 (sine at 180°)
    assert!(SINE_LUT[128].abs() < 100, "LUT[128] should be near zero");

    // Index 192 should be ~min (sine trough at 270°)
    assert!(SINE_LUT[192] < -30000, "LUT[192] should be near min");
}

#[test]
fn test_lut_symmetry() {
    // First half positive peak, second half negative
    for i in 0..64 {
        assert!(SINE_LUT[i] >= 0 || i == 0, "First quadrant should be non-negative");
    }
    for i in 128..192 {
        assert!(SINE_LUT[i] <= 0, "Third quadrant should be non-positive");
    }
}

#[test]
fn test_lut_amplitude() {
    let max = SINE_LUT.iter().max().copied().unwrap();
    let min = SINE_LUT.iter().min().copied().unwrap();

    // Should use full i16 range
    assert!(max > 32000, "Max should be near i16::MAX");
    assert!(min < -32000, "Min should be near i16::MIN");
}
