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

    /// Local keying output state (post-Iambic processing)
    ///
    /// true = key down (transmitting)
    /// false = key up (idle)
    ///
    /// This is the final keying decision after Iambic FSM processing.
    /// Both local and remote TX consumers read this field.
    pub local_key: bool,

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
    /// Empty sample constant for initialization.
    ///
    /// Used for buffer initialization and testing.
    pub const EMPTY: Self = Self {
        gpio: GpioState::IDLE,
        local_key: false,
        audio_level: 0,
        flags: 0,
        config_gen: 0,
    };

    /// Create a new sample with all fields zero/false
    ///
    /// Used for initialization and testing.
    #[inline]
    pub const fn zero() -> Self {
        Self::EMPTY
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
    #[inline]
    pub fn is_silent(&self) -> bool {
        self.gpio.is_idle()
            && !self.local_key
            && self.audio_level == 0
            && (self.flags & FLAG_GPIO_EDGE) == 0
    }

    /// Check if this sample is a silence marker (RLE compressed).
    ///
    /// Silence markers have FLAG_SILENCE set and encode the tick count
    /// in the config_gen field.
    #[inline]
    pub fn is_silence(&self) -> bool {
        (self.flags & FLAG_SILENCE) != 0
    }

    /// Create a silence marker for RLE compression.
    ///
    /// # Arguments
    /// * `ticks` - Number of silent ticks this marker represents
    ///
    /// The tick count is stored in config_gen (up to 65535 ticks).
    #[inline]
    pub const fn silence(ticks: u32) -> Self {
        Self {
            gpio: GpioState::IDLE,
            local_key: false,
            audio_level: 0,
            flags: FLAG_SILENCE,
            config_gen: ticks as u16, // Truncates to u16
        }
    }

    /// Get the number of silent ticks this marker represents.
    ///
    /// Only valid when is_silence() returns true.
    #[inline]
    pub fn silence_ticks(&self) -> u32 {
        self.config_gen as u32
    }

    /// Check if this sample has any change from a previous sample.
    ///
    /// Used by stream for silence compression - only write samples
    /// when state actually changes.
    #[inline]
    pub fn has_change_from(&self, other: &Self) -> bool {
        self.gpio != other.gpio
            || self.local_key != other.local_key
            || self.audio_level != other.audio_level
    }

    /// Create a copy of this sample with edge flags set based on previous.
    ///
    /// Sets FLAG_GPIO_EDGE if GPIO changed, adds local_key edge flag.
    #[inline]
    pub fn with_edges_from(&self, prev: &Self) -> Self {
        let mut flags = self.flags;
        if self.gpio != prev.gpio {
            flags |= FLAG_GPIO_EDGE;
        }
        if self.local_key != prev.local_key {
            flags |= FLAG_LOCAL_EDGE;
        }
        Self { flags, ..*self }
    }

    /// Check if local key had an edge transition at this sample.
    #[inline]
    pub fn has_local_edge(&self) -> bool {
        (self.flags & FLAG_LOCAL_EDGE) != 0
    }

    /// Check if GPIO had an edge transition at this sample.
    #[inline]
    pub fn has_gpio_edge(&self) -> bool {
        (self.flags & FLAG_GPIO_EDGE) != 0
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
pub struct GpioState {
    /// Raw GPIO bits. Public for direct construction in tests.
    pub bits: u8,
}

impl GpioState {
    /// DIT paddle bit mask (bit 0)
    pub const DIT: u8 = 0x01;

    /// DAH paddle bit mask (bit 1)
    pub const DAH: u8 = 0x02;

    /// Idle state constant (no paddles pressed)
    pub const IDLE: Self = Self { bits: 0 };

    /// Both paddles pressed (squeeze)
    pub const BOTH: Self = Self { bits: Self::DIT | Self::DAH };

    /// Create GPIO state from raw byte
    ///
    /// # Arguments
    /// * `bits` - Raw GPIO bits (only bits 0-1 used currently)
    #[inline]
    pub const fn from_bits(bits: u8) -> Self {
        Self { bits }
    }

    /// Check if DIT paddle is pressed
    #[inline]
    pub const fn dit(&self) -> bool {
        (self.bits & Self::DIT) != 0
    }

    /// Check if DAH paddle is pressed
    #[inline]
    pub const fn dah(&self) -> bool {
        (self.bits & Self::DAH) != 0
    }

    /// Check if no paddles are pressed (idle)
    #[inline]
    pub const fn is_idle(&self) -> bool {
        self.bits == 0
    }

    /// Check if both paddles are pressed (squeeze)
    #[inline]
    pub const fn both(&self) -> bool {
        (self.bits & (Self::DIT | Self::DAH)) == (Self::DIT | Self::DAH)
    }

    /// Check if only DIT is pressed (not DAH)
    #[inline]
    pub const fn dit_only(&self) -> bool {
        self.bits == Self::DIT
    }

    /// Check if only DAH is pressed (not DIT)
    #[inline]
    pub const fn dah_only(&self) -> bool {
        self.bits == Self::DAH
    }

    /// Create new idle GPIO state.
    ///
    /// Equivalent to `GpioState::IDLE`.
    #[inline]
    pub const fn new() -> Self {
        Self::IDLE
    }

    /// Set DIT paddle state.
    #[inline]
    pub fn set_dit(&mut self, pressed: bool) {
        if pressed {
            self.bits |= Self::DIT;
        } else {
            self.bits &= !Self::DIT;
        }
    }

    /// Set DAH paddle state.
    #[inline]
    pub fn set_dah(&mut self, pressed: bool) {
        if pressed {
            self.bits |= Self::DAH;
        } else {
            self.bits &= !Self::DAH;
        }
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

/// Local key edge detected (key up/down transition)
///
/// Set when local_key transitions between states.
/// Used by consumers to detect keying edges without comparing samples.
pub const FLAG_LOCAL_EDGE: u8 = 0x20;

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
        assert!(!sample.local_key);
        assert_eq!(sample.audio_level, 0);
        assert_eq!(sample.flags, 0);
        assert_eq!(sample.config_gen, 0);
    }

    #[test]
    fn test_stream_sample_empty() {
        let sample = StreamSample::EMPTY;
        assert_eq!(sample.gpio, GpioState::IDLE);
        assert!(!sample.local_key);
        assert_eq!(sample.audio_level, 0);
        assert_eq!(sample.flags, 0);
        assert_eq!(sample.config_gen, 0);
    }

    #[test]
    fn test_stream_sample_is_silent() {
        let silent = StreamSample::zero();
        assert!(silent.is_silent());

        let mut not_silent = StreamSample::zero();
        not_silent.local_key = true;
        assert!(!not_silent.is_silent());

        not_silent = StreamSample::zero();
        not_silent.audio_level = 100;
        assert!(!not_silent.is_silent());

        not_silent = StreamSample::zero();
        not_silent.gpio = GpioState::from_bits(GpioState::DIT);
        assert!(!not_silent.is_silent());
    }

    #[test]
    fn test_silence_marker() {
        let marker = StreamSample::silence(1000);
        assert!(marker.is_silence());
        assert_eq!(marker.silence_ticks(), 1000);
        assert_eq!(marker.flags, FLAG_SILENCE);
    }

    #[test]
    fn test_has_change_from() {
        let base = StreamSample::EMPTY;

        let mut changed = StreamSample::EMPTY;
        changed.local_key = true;
        assert!(changed.has_change_from(&base));

        changed = StreamSample::EMPTY;
        changed.gpio = GpioState::from_bits(GpioState::DIT);
        assert!(changed.has_change_from(&base));

        changed = StreamSample::EMPTY;
        changed.audio_level = 100;
        assert!(changed.has_change_from(&base));

        // Same state = no change
        assert!(!base.has_change_from(&base));
    }

    #[test]
    fn test_with_edges_from() {
        let prev = StreamSample::EMPTY;
        let mut current = StreamSample::EMPTY;
        current.local_key = true;

        let with_edges = current.with_edges_from(&prev);
        assert!(with_edges.has_local_edge());
        assert!(!with_edges.has_gpio_edge());

        // GPIO edge
        let mut gpio_change = StreamSample::EMPTY;
        gpio_change.gpio = GpioState::from_bits(GpioState::DIT);
        let with_gpio_edge = gpio_change.with_edges_from(&prev);
        assert!(with_gpio_edge.has_gpio_edge());
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
