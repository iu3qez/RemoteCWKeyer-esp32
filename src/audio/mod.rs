//! Audio subsystem for sidetone and remote audio
//!
//! Architecture:
//! - Sidetone generator: LUT + phase accumulator, digital fade
//! - Ring buffers: 64 samples (sidetone), 512 (remote)
//! - PTT state machine controls audio source
//! - ES8311 codec via I2S @ 8 kHz

pub mod lut;

pub use lut::{SINE_LUT, LUT_SIZE};
