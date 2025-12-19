//! Module: sample
//!
//! Purpose: StreamSample types for keying events. Represents a single time-slice
//! (tick) of keyer state at a specific moment in time.
//!
//! Architecture:
//! - Compact 6-byte structure for memory efficiency (ARCHITECTURE.md Rule 9)
//! - All samples time-aligned at TICK_RATE_HZ (typically 10 kHz)
//! - Silence is data (explicitly represented, can be compressed with RLE)
//!
//! Safety: Safe. No unsafe blocks. Copy types only.

/// A single sample in the keying stream
///
/// Represents one time-slice (tick) of the keyer state. All samples are
/// time-aligned at the configured tick rate (typically 10 kHz = 100µs period).
///
/// Size: 6 bytes (compact for PSRAM efficiency)
///
/// Memory layout:
/// ```text
/// [gpio:1][key:1][audio_level:1][flags:1][config_gen:2] = 6 bytes
/// ```
///
/// At 10 kHz tick rate:
/// - 10,000 samples/second
/// - 60,000 bytes/second worst-case (no compression)
/// - ~3.6 MB for 1 minute uncompressed
/// - Typical: 90% silence, ~360 KB/minute with RLE
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StreamSample {
    /// GPIO paddle state (raw hardware input)
    ///
    /// Bit 0: DIT paddle (1 = pressed, 0 = released)
    /// Bit 1: DAH paddle (1 = pressed, 0 = released)
    /// Remaining bits: reserved for future use
    pub gpio: GpioState,

    /// Keying output state (post-Iambic processing)
    ///
    /// true = key down (transmitting)
    /// false = key up (idle)
    ///
    /// This is the final keying decision after Iambic FSM processing.
    /// Both local and remote TX consumers read this field.
    pub key: bool,

    /// Audio sidetone level with envelope applied
    ///
    /// 0 = silent
    /// 255 = full volume
    ///
    /// Includes fade in/out envelope to eliminate clicks.
    /// Calculated by AudioEnvelope in RT thread.
    pub audio_level: u8,

    /// Sample flags (edges, state transitions, markers)
    ///
    /// See FLAG_* constants for bit definitions.
    /// Multiple flags can be set simultaneously (bitwise OR).
    pub flags: u8,

    /// Configuration generation number
    ///
    /// Copied from CONFIG.generation at sample creation time.
    /// Incremented when any config parameter changes.
    ///
    /// Consumers can detect config changes by comparing this value
    /// with their cached generation number.
    pub config_gen: u16,
}

impl StreamSample {
    /// Create a new sample with all fields zero/false
    ///
    /// Used for initialization and testing.
    pub const fn zero() -> Self {
        Self {
            gpio: GpioState::IDLE,
            key: false,
            audio_level: 0,
            flags: 0,
            config_gen: 0,
        }
    }

    /// Check if this sample represents silence (no activity)
    ///
    /// A sample is considered silent when:
    /// - No GPIO activity (paddles released)
    /// - Key is up (not transmitting)
    /// - Audio level is zero
    /// - No edge transitions
    ///
    /// Silent samples are candidates for RLE compression.
    pub fn is_silent(&self) -> bool {
        self.gpio.is_idle()
            && !self.key
            && self.audio_level == 0
            && (self.flags & FLAG_GPIO_EDGE) == 0
    }

    /// Check if config changed at this sample
    pub fn config_changed(&self) -> bool {
        (self.flags & FLAG_CONFIG_CHANGE) != 0
    }

    /// Check if TX mode started at this sample
    pub fn tx_started(&self) -> bool {
        (self.flags & FLAG_TX_START) != 0
    }

    /// Check if RX mode started at this sample
    pub fn rx_started(&self) -> bool {
        (self.flags & FLAG_RX_START) != 0
    }
}

/// GPIO paddle state
///
/// Represents the physical state of DIT and DAH paddle inputs.
/// Stored as a single byte with bit flags for memory efficiency.
///
/// Bit layout:
/// - Bit 0: DIT paddle
/// - Bit 1: DAH paddle
/// - Bits 2-7: Reserved (future: straight key, additional inputs)
#[repr(transparent)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GpioState(u8);

impl GpioState {
    /// DIT paddle bit mask (bit 0)
    pub const DIT: u8 = 0x01;

    /// DAH paddle bit mask (bit 1)
    pub const DAH: u8 = 0x02;

    /// Idle state constant (no paddles pressed)
    pub const IDLE: Self = Self(0);

    /// Both paddles pressed (squeeze)
    pub const BOTH: Self = Self(Self::DIT | Self::DAH);

    /// Create GPIO state from raw byte
    ///
    /// # Arguments
    /// * `bits` - Raw GPIO bits (only bits 0-1 used currently)
    pub const fn from_bits(bits: u8) -> Self {
        Self(bits)
    }

    /// Get raw bits value
    pub const fn bits(&self) -> u8 {
        self.0
    }

    /// Check if DIT paddle is pressed
    pub const fn dit(&self) -> bool {
        (self.0 & Self::DIT) != 0
    }

    /// Check if DAH paddle is pressed
    pub const fn dah(&self) -> bool {
        (self.0 & Self::DAH) != 0
    }

    /// Check if no paddles are pressed (idle)
    pub const fn is_idle(&self) -> bool {
        self.0 == 0
    }

    /// Check if both paddles are pressed (squeeze)
    pub const fn both(&self) -> bool {
        (self.0 & (Self::DIT | Self::DAH)) == (Self::DIT | Self::DAH)
    }

    /// Check if only DIT is pressed (not DAH)
    pub const fn dit_only(&self) -> bool {
        self.0 == Self::DIT
    }

    /// Check if only DAH is pressed (not DIT)
    pub const fn dah_only(&self) -> bool {
        self.0 == Self::DAH
    }
}

// ============================================================================
// Sample Flags
// ============================================================================

/// GPIO state changed this tick (edge detected)
///
/// Set when GPIO transitions from one state to another.
/// Used for edge detection and timeline analysis.
pub const FLAG_GPIO_EDGE: u8 = 0x01;

/// Configuration changed at this sample
///
/// Set when CONFIG.generation incremented and applied.
/// Signals to consumers that they should reload config parameters.
pub const FLAG_CONFIG_CHANGE: u8 = 0x02;

/// TX mode started (transition from RX to TX)
///
/// Set on first sample after GPIO ISR wakes RT thread.
/// Marks beginning of transmission session.
pub const FLAG_TX_START: u8 = 0x04;

/// RX mode started (transition from TX to RX)
///
/// Set on first sample after PTT tail timeout.
/// Marks end of transmission session.
pub const FLAG_RX_START: u8 = 0x08;

/// Silence marker for RLE compression
///
/// Set on samples that represent compressed silence.
/// Used by stream replay and diagnostics.
pub const FLAG_SILENCE: u8 = 0x10;

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_stream_sample_size() {
        // Verify sample is exactly 6 bytes as specified
        assert_eq!(core::mem::size_of::<StreamSample>(), 6);
    }

    #[test]
    fn test_stream_sample_zero() {
        let sample = StreamSample::zero();
        assert_eq!(sample.gpio, GpioState::IDLE);
        assert_eq!(sample.key, false);
        assert_eq!(sample.audio_level, 0);
        assert_eq!(sample.flags, 0);
        assert_eq!(sample.config_gen, 0);
    }

    #[test]
    fn test_stream_sample_is_silent() {
        let silent = StreamSample::zero();
        assert!(silent.is_silent());

        let mut not_silent = StreamSample::zero();
        not_silent.key = true;
        assert!(!not_silent.is_silent());

        not_silent = StreamSample::zero();
        not_silent.audio_level = 100;
        assert!(!not_silent.is_silent());

        not_silent = StreamSample::zero();
        not_silent.gpio = GpioState::from_bits(GpioState::DIT);
        assert!(!not_silent.is_silent());
    }

    #[test]
    fn test_gpio_state_dit() {
        let dit = GpioState::from_bits(GpioState::DIT);
        assert!(dit.dit());
        assert!(!dit.dah());
        assert!(!dit.is_idle());
        assert!(!dit.both());
        assert!(dit.dit_only());
        assert!(!dit.dah_only());
    }

    #[test]
    fn test_gpio_state_dah() {
        let dah = GpioState::from_bits(GpioState::DAH);
        assert!(!dah.dit());
        assert!(dah.dah());
        assert!(!dah.is_idle());
        assert!(!dah.both());
        assert!(!dah.dit_only());
        assert!(dah.dah_only());
    }

    #[test]
    fn test_gpio_state_both() {
        let both = GpioState::BOTH;
        assert!(both.dit());
        assert!(both.dah());
        assert!(!both.is_idle());
        assert!(both.both());
        assert!(!both.dit_only());
        assert!(!both.dah_only());
    }

    #[test]
    fn test_gpio_state_idle() {
        let idle = GpioState::IDLE;
        assert!(!idle.dit());
        assert!(!idle.dah());
        assert!(idle.is_idle());
        assert!(!idle.both());
        assert!(!idle.dit_only());
        assert!(!idle.dah_only());
    }

    #[test]
    fn test_flag_checking() {
        let mut sample = StreamSample::zero();

        sample.flags = FLAG_CONFIG_CHANGE;
        assert!(sample.config_changed());
        assert!(!sample.tx_started());
        assert!(!sample.rx_started());

        sample.flags = FLAG_TX_START | FLAG_GPIO_EDGE;
        assert!(sample.tx_started());
        assert!(!sample.config_changed());
        assert!(!sample.rx_started());

        sample.flags = FLAG_RX_START;
        assert!(sample.rx_started());
        assert!(!sample.config_changed());
        assert!(!sample.tx_started());
    }
}
