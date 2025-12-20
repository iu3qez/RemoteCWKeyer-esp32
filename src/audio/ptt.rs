//! PTT (Push-To-Talk) state machine
//!
//! Controls TX/RX switching based on audio output.
//! PTT activates immediately on first audio sample,
//! deactivates after tail timeout from last audio.

/// PTT state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PttState {
    /// Not transmitting (RX mode)
    Off,
    /// Transmitting (TX mode)
    On,
}

/// PTT controller
///
/// Manages PTT state based on audio activity.
pub struct PttController {
    /// Current PTT state
    state: PttState,
    /// Tail timeout in microseconds
    tail_us: u64,
    /// Timestamp of last audio sample (microseconds)
    last_audio_us: u64,
    /// Whether we've seen any audio since last Off
    audio_active: bool,
}

impl PttController {
    /// Create new PTT controller
    ///
    /// # Arguments
    /// * `tail_ms` - Tail timeout in milliseconds
    pub fn new(tail_ms: u32) -> Self {
        Self {
            state: PttState::Off,
            tail_us: tail_ms as u64 * 1000,
            last_audio_us: 0,
            audio_active: false,
        }
    }

    /// Get current PTT state
    #[inline]
    pub fn state(&self) -> PttState {
        self.state
    }

    /// Check if PTT is on
    #[inline]
    pub fn is_on(&self) -> bool {
        self.state == PttState::On
    }

    /// Notify controller of audio sample output
    ///
    /// # Arguments
    /// * `timestamp_us` - Current time in microseconds
    ///
    /// Call this for each audio sample that represents actual keying
    /// (not silence). This triggers PTT on and resets the tail timer.
    #[inline]
    pub fn audio_sample(&mut self, timestamp_us: u64) {
        self.last_audio_us = timestamp_us;
        self.audio_active = true;

        if self.state == PttState::Off {
            self.state = PttState::On;
        }
    }

    /// Periodic tick to check for tail timeout
    ///
    /// # Arguments
    /// * `timestamp_us` - Current time in microseconds
    ///
    /// Call this periodically (e.g., every audio buffer) to check
    /// if the tail timeout has expired.
    #[inline]
    pub fn tick(&mut self, timestamp_us: u64) {
        if self.state == PttState::On && self.audio_active {
            // Check if tail has expired
            let elapsed = timestamp_us.saturating_sub(self.last_audio_us);
            if elapsed >= self.tail_us {
                self.state = PttState::Off;
                self.audio_active = false;
            }
        }
    }

    /// Update tail timeout (e.g., when config changes)
    #[inline]
    pub fn set_tail_ms(&mut self, tail_ms: u32) {
        self.tail_us = tail_ms as u64 * 1000;
    }

    /// Force PTT off immediately
    #[inline]
    pub fn force_off(&mut self) {
        self.state = PttState::Off;
        self.audio_active = false;
    }

    /// Reset to initial state
    #[inline]
    pub fn reset(&mut self) {
        self.state = PttState::Off;
        self.last_audio_us = 0;
        self.audio_active = false;
    }
}
