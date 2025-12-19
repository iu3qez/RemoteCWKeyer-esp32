//! Stream sample types for RustRemoteCWKeyer.
//!
//! Each sample captures the state of all three channels (GPIO, local keyer, remote CW)
//! at a single point in time. Samples are time-aligned at the stream tick rate.

/// Edge flags for StreamSample
pub const FLAG_GPIO_EDGE: u8 = 0x01;
pub const FLAG_LOCAL_EDGE: u8 = 0x02;
pub const FLAG_REMOTE_EDGE: u8 = 0x04;
pub const FLAG_SILENCE: u8 = 0x80;

/// Physical paddle GPIO state.
///
/// Packed into a single byte for efficiency.
/// - bit 0: DIT paddle pressed
/// - bit 1: DAH paddle pressed
/// - bit 2: Straight key pressed
#[derive(Clone, Copy, Default, PartialEq, Eq)]
#[repr(transparent)]
pub struct GpioState {
    pub bits: u8,
}

impl GpioState {
    pub const IDLE: Self = Self { bits: 0 };

    #[inline]
    pub const fn new() -> Self {
        Self { bits: 0 }
    }

    #[inline]
    pub const fn dit(&self) -> bool {
        self.bits & 0x01 != 0
    }

    #[inline]
    pub const fn dah(&self) -> bool {
        self.bits & 0x02 != 0
    }

    #[inline]
    pub const fn straight_key(&self) -> bool {
        self.bits & 0x04 != 0
    }

    #[inline]
    pub fn set_dit(&mut self, pressed: bool) {
        if pressed {
            self.bits |= 0x01;
        } else {
            self.bits &= !0x01;
        }
    }

    #[inline]
    pub fn set_dah(&mut self, pressed: bool) {
        if pressed {
            self.bits |= 0x02;
        } else {
            self.bits &= !0x02;
        }
    }

    #[inline]
    pub fn set_straight_key(&mut self, pressed: bool) {
        if pressed {
            self.bits |= 0x04;
        } else {
            self.bits &= !0x04;
        }
    }

    /// Returns true if any paddle/key is pressed.
    #[inline]
    pub const fn any_pressed(&self) -> bool {
        self.bits != 0
    }

    /// Returns true if both DIT and DAH are pressed (squeeze).
    #[inline]
    pub const fn is_squeeze(&self) -> bool {
        (self.bits & 0x03) == 0x03
    }
}

impl core::fmt::Debug for GpioState {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "GPIO[{}{}{}]",
            if self.dit() { "D" } else { "." },
            if self.dah() { "A" } else { "." },
            if self.straight_key() { "K" } else { "." },
        )
    }
}

/// A single time-aligned sample of all keying channels.
///
/// Size: 4 bytes, aligned to 4 bytes for atomic-friendly access.
///
/// # Channels
/// - `gpio`: Physical paddle state (DIT, DAH, straight key)
/// - `local_key`: Output of iambic/manual keyer (drives audio + TX)
/// - `remote_key`: CW received from remote station
/// - `flags`: Edge detection and silence compression markers
#[derive(Clone, Copy, Default)]
#[repr(C, align(4))]
pub struct StreamSample {
    pub gpio: GpioState,
    pub local_key: bool,
    pub remote_key: bool,
    pub flags: u8,
}

impl StreamSample {
    /// Empty sample (all channels idle).
    pub const EMPTY: Self = Self {
        gpio: GpioState::IDLE,
        local_key: false,
        remote_key: false,
        flags: 0,
    };

    /// Create a silence marker encoding N idle ticks.
    ///
    /// Silence markers compress consecutive identical samples.
    /// Max encodable ticks: 2^23 - 1 = 8,388,607 (~14 minutes @ 10kHz)
    #[inline]
    pub fn silence(ticks: u32) -> Self {
        Self {
            gpio: GpioState {
                bits: (ticks & 0xFF) as u8,
            },
            local_key: (ticks >> 8) & 1 != 0,
            remote_key: (ticks >> 9) & 1 != 0,
            flags: FLAG_SILENCE | (((ticks >> 16) & 0x7F) as u8),
        }
    }

    /// Check if this sample is a silence marker.
    #[inline]
    pub const fn is_silence(&self) -> bool {
        self.flags & FLAG_SILENCE != 0
    }

    /// Decode silence tick count (only valid if `is_silence()` is true).
    #[inline]
    pub const fn silence_ticks(&self) -> u32 {
        if !self.is_silence() {
            return 0;
        }
        (self.gpio.bits as u32)
            | ((self.local_key as u32) << 8)
            | ((self.remote_key as u32) << 9)
            | (((self.flags & 0x7F) as u32) << 16)
    }

    /// Check if any channel changed from the previous sample.
    #[inline]
    pub fn has_change_from(&self, prev: &Self) -> bool {
        self.gpio.bits != prev.gpio.bits
            || self.local_key != prev.local_key
            || self.remote_key != prev.remote_key
    }

    /// Create a copy of this sample with edge flags computed from previous sample.
    #[inline]
    pub fn with_edges_from(&self, prev: &Self) -> Self {
        let mut flags = 0u8;
        if self.gpio.bits != prev.gpio.bits {
            flags |= FLAG_GPIO_EDGE;
        }
        if self.local_key != prev.local_key {
            flags |= FLAG_LOCAL_EDGE;
        }
        if self.remote_key != prev.remote_key {
            flags |= FLAG_REMOTE_EDGE;
        }
        Self { flags, ..*self }
    }

    /// Check if GPIO changed in this sample.
    #[inline]
    pub const fn has_gpio_edge(&self) -> bool {
        self.flags & FLAG_GPIO_EDGE != 0
    }

    /// Check if local keyer output changed in this sample.
    #[inline]
    pub const fn has_local_edge(&self) -> bool {
        self.flags & FLAG_LOCAL_EDGE != 0
    }

    /// Check if remote CW changed in this sample.
    #[inline]
    pub const fn has_remote_edge(&self) -> bool {
        self.flags & FLAG_REMOTE_EDGE != 0
    }
}

impl core::fmt::Debug for StreamSample {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        if self.is_silence() {
            write!(f, "Silence({})", self.silence_ticks())
        } else {
            write!(
                f,
                "Sample({:?}, L={}, R={}, f={:02x})",
                self.gpio, self.local_key as u8, self.remote_key as u8, self.flags
            )
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gpio_state_basic() {
        let mut gpio = GpioState::new();
        assert!(!gpio.dit());
        assert!(!gpio.dah());
        assert!(!gpio.any_pressed());

        gpio.set_dit(true);
        assert!(gpio.dit());
        assert!(!gpio.dah());
        assert!(gpio.any_pressed());

        gpio.set_dah(true);
        assert!(gpio.dit());
        assert!(gpio.dah());
        assert!(gpio.is_squeeze());
    }

    #[test]
    fn test_silence_encoding() {
        // Small value
        let s = StreamSample::silence(42);
        assert!(s.is_silence());
        assert_eq!(s.silence_ticks(), 42);

        // Large value
        let s = StreamSample::silence(1_000_000);
        assert!(s.is_silence());
        assert_eq!(s.silence_ticks(), 1_000_000);

        // Max value (23 bits)
        let s = StreamSample::silence(0x7FFFFF);
        assert!(s.is_silence());
        assert_eq!(s.silence_ticks(), 0x7FFFFF);
    }

    #[test]
    fn test_edge_detection() {
        let prev = StreamSample::EMPTY;
        let mut curr = StreamSample::EMPTY;
        curr.local_key = true;

        let with_edges = curr.with_edges_from(&prev);
        assert!(with_edges.has_local_edge());
        assert!(!with_edges.has_gpio_edge());
        assert!(!with_edges.has_remote_edge());
    }

    #[test]
    fn test_sample_size() {
        assert_eq!(core::mem::size_of::<StreamSample>(), 4);
        assert_eq!(core::mem::align_of::<StreamSample>(), 4);
    }
}
