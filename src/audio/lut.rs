//! Sine wave lookup table for sidetone generation
//!
//! 256-entry table covering one full cycle.
//! Values are i16 for direct use with I2S.

/// Number of entries in the sine LUT
pub const LUT_SIZE: usize = 256;

/// Pre-computed sine wave lookup table
///
/// 256 samples covering 0 to 2π
/// Amplitude: full i16 range (-32767 to +32767)
/// Index 0 = 0°, 64 = 90°, 128 = 180°, 192 = 270°
pub static SINE_LUT: [i16; LUT_SIZE] = {
    let mut table = [0i16; LUT_SIZE];
    let mut i = 0;
    while i < LUT_SIZE {
        // sin(2π * i / 256) * 32767
        // Using Taylor series approximation for const evaluation
        let angle = (i as f64) * core::f64::consts::PI * 2.0 / (LUT_SIZE as f64);
        let sin_val = const_sin(angle);
        table[i] = (sin_val * 32767.0) as i16;
        i += 1;
    }
    table
};

/// Const-compatible sine approximation using Taylor series
const fn const_sin(x: f64) -> f64 {
    // Normalize to [-π, π]
    let mut x = x;
    while x > core::f64::consts::PI {
        x -= 2.0 * core::f64::consts::PI;
    }
    while x < -core::f64::consts::PI {
        x += 2.0 * core::f64::consts::PI;
    }

    // Taylor series: sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...
    let x2 = x * x;
    let x3 = x2 * x;
    let x5 = x3 * x2;
    let x7 = x5 * x2;
    let x9 = x7 * x2;

    x - x3 / 6.0 + x5 / 120.0 - x7 / 5040.0 + x9 / 362880.0
}
