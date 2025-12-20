//! Sidetone generator with phase accumulator and fade envelope
//!
//! Uses lookup table for sine wave, fixed-point phase accumulator
//! for frequency control, and linear fade for anti-click.

use super::lut::{SINE_LUT, LUT_SIZE};

/// Fade envelope state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FadeState {
    /// Output is zero
    Silent,
    /// Ramping up (0 → full)
    FadeIn,
    /// Full amplitude output
    Sustain,
    /// Ramping down (full → 0)
    FadeOut,
}

/// Sidetone generator
///
/// Generates sine wave with configurable frequency and fade envelope.
/// Uses phase accumulator for frequency control (no floating point in hot path).
pub struct SidetoneGen {
    /// Phase accumulator (32-bit fixed point, top 8 bits = LUT index)
    phase: u32,
    /// Phase increment per sample (determines frequency)
    phase_inc: u32,
    /// Current fade state
    fade_state: FadeState,
    /// Current fade position (0 to fade_len)
    fade_pos: u16,
    /// Fade length in samples
    fade_len: u16,
}

impl SidetoneGen {
    /// Create new sidetone generator
    ///
    /// # Arguments
    /// * `freq_hz` - Sidetone frequency in Hz (typically 400-800)
    /// * `sample_rate` - Output sample rate in Hz (typically 8000)
    /// * `fade_samples` - Fade duration in samples (e.g., 40 for 5ms @ 8kHz)
    pub fn new(freq_hz: u32, sample_rate: u32, fade_samples: u16) -> Self {
        Self {
            phase: 0,
            phase_inc: Self::calc_phase_inc(freq_hz, sample_rate),
            fade_state: FadeState::Silent,
            fade_pos: 0,
            fade_len: fade_samples.max(1), // Avoid div by zero
        }
    }

    /// Calculate phase increment for target frequency
    ///
    /// phase_inc = (freq * 2^32) / sample_rate
    #[inline]
    fn calc_phase_inc(freq_hz: u32, sample_rate: u32) -> u32 {
        ((freq_hz as u64 * (1u64 << 32)) / sample_rate as u64) as u32
    }

    /// Update frequency (e.g., when config changes)
    #[inline]
    pub fn set_frequency(&mut self, freq_hz: u32, sample_rate: u32) {
        self.phase_inc = Self::calc_phase_inc(freq_hz, sample_rate);
    }

    /// Get current fade state
    #[inline]
    pub fn fade_state(&self) -> FadeState {
        self.fade_state
    }

    /// Generate next audio sample
    ///
    /// # Arguments
    /// * `key_down` - true if key is pressed (generate tone), false for silence
    ///
    /// # Returns
    /// Audio sample in i16 range
    #[inline]
    pub fn next_sample(&mut self, key_down: bool) -> i16 {
        // Update fade state and get amplitude multiplier
        let amplitude = self.update_fade(key_down);

        if amplitude == 0 {
            return 0;
        }

        // LUT lookup with phase accumulator
        let idx = (self.phase >> 24) as usize; // Top 8 bits = index (0-255)
        self.phase = self.phase.wrapping_add(self.phase_inc);

        // Get sample from LUT
        let sample = SINE_LUT[idx % LUT_SIZE];

        // Apply fade envelope
        ((sample as i32 * amplitude as i32) >> 16) as i16
    }

    /// Update fade state machine, returns amplitude (0-65535)
    #[inline]
    fn update_fade(&mut self, key_down: bool) -> u16 {
        match (self.fade_state, key_down) {
            // Silent + key down → start fade in
            (FadeState::Silent, true) => {
                self.fade_state = FadeState::FadeIn;
                self.fade_pos = 0;
                0
            }

            // Fade in + key down → continue fade
            (FadeState::FadeIn, true) => {
                self.fade_pos += 1;
                if self.fade_pos >= self.fade_len {
                    self.fade_state = FadeState::Sustain;
                    0xFFFF
                } else {
                    ((self.fade_pos as u32 * 0xFFFF) / self.fade_len as u32) as u16
                }
            }

            // Sustain + key down → full amplitude
            (FadeState::Sustain, true) => 0xFFFF,

            // Key up while fade in or sustain → start fade out
            (FadeState::FadeIn, false) | (FadeState::Sustain, false) => {
                self.fade_state = FadeState::FadeOut;
                self.fade_pos = self.fade_len;
                0xFFFF
            }

            // Fade out → continue fade
            (FadeState::FadeOut, _) => {
                if self.fade_pos == 0 {
                    self.fade_state = FadeState::Silent;
                    0
                } else {
                    self.fade_pos -= 1;
                    ((self.fade_pos as u32 * 0xFFFF) / self.fade_len as u32) as u16
                }
            }

            // Silent + key up → stay silent
            (FadeState::Silent, false) => 0,
        }
    }

    /// Reset generator state (e.g., after fault)
    pub fn reset(&mut self) {
        self.phase = 0;
        self.fade_state = FadeState::Silent;
        self.fade_pos = 0;
    }
}
