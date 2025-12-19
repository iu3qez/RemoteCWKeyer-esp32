//! Audio HAL for I2S sidetone output.

// TODO: Implement with esp-idf-hal::i2s

/// Audio configuration.
pub struct AudioConfig {
    pub sample_rate: u32,
    pub sidetone_freq: u32,
    pub volume: u8,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: 44100,
            sidetone_freq: 700,
            volume: 80,
        }
    }
}
