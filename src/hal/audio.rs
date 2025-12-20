//! Audio HAL (legacy stub)
//!
//! NOTE: Main audio implementation is in src/audio/ module.
//! This file kept for backwards compatibility.
//!
//! See:
//! - src/audio/sidetone.rs - Sidetone generator
//! - src/audio/buffer.rs - Ring buffers
//! - src/audio/ptt.rs - PTT state machine
//! - src/hal/es8311.rs - ES8311 codec driver

/// Audio configuration (legacy, use audio module instead)
#[deprecated(note = "Use audio::SidetoneGen instead")]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub sidetone_freq: u32,
    pub volume: u8,
}

#[allow(deprecated)]
impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: 8000,
            sidetone_freq: 700,
            volume: 80,
        }
    }
}
