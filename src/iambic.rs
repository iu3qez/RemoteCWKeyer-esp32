//! Iambic keyer finite state machine.
//!
//! Pure logic, no hardware dependencies. Consumes GPIO state,
//! produces keying output. Fully testable on host.
//!
//! # Iambic Modes
//!
//! - **Mode A**: Stops immediately when paddles released
//! - **Mode B**: Completes current element + one more when squeeze released

use crate::sample::{GpioState, StreamSample};

/// Iambic keyer mode.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IambicMode {
    /// Mode A: Stop immediately when paddles released.
    A,
    /// Mode B: Complete current + one bonus element on squeeze release.
    B,
}

impl Default for IambicMode {
    fn default() -> Self {
        IambicMode::B
    }
}

/// Keying element type.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Element {
    Dit,
    Dah,
}

impl Element {
    /// Get the opposite element.
    #[inline]
    pub fn opposite(self) -> Self {
        match self {
            Element::Dit => Element::Dah,
            Element::Dah => Element::Dit,
        }
    }
}

/// FSM state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum State {
    Idle,
    SendDit,
    SendDah,
    Gap,
}

/// Iambic keyer configuration.
#[derive(Clone, Copy, Debug)]
pub struct IambicConfig {
    /// Speed in words per minute (PARIS timing).
    pub wpm: u32,

    /// Iambic mode (A or B).
    pub mode: IambicMode,

    /// Dit memory enable.
    pub dit_memory: bool,

    /// Dah memory enable.
    pub dah_memory: bool,
}

impl Default for IambicConfig {
    fn default() -> Self {
        Self {
            wpm: 20,
            mode: IambicMode::B,
            dit_memory: true,
            dah_memory: true,
        }
    }
}

impl IambicConfig {
    /// Create config for given WPM with default settings.
    pub fn with_wpm(wpm: u32) -> Self {
        Self {
            wpm,
            ..Default::default()
        }
    }

    /// Calculate dit duration in microseconds.
    ///
    /// PARIS timing: dit = 1.2 / WPM seconds
    #[inline]
    pub fn dit_duration_us(&self) -> i64 {
        1_200_000 / self.wpm as i64
    }

    /// Calculate dah duration in microseconds (3x dit).
    #[inline]
    pub fn dah_duration_us(&self) -> i64 {
        self.dit_duration_us() * 3
    }

    /// Calculate inter-element gap in microseconds (1x dit).
    #[inline]
    pub fn gap_duration_us(&self) -> i64 {
        self.dit_duration_us()
    }
}

/// Iambic keyer processor.
///
/// Pure FSM that converts paddle GPIO state into keying output.
/// No hardware dependencies, fully testable on host.
///
/// # Example
///
/// ```
/// use rust_remote_cw_keyer::iambic::{IambicProcessor, IambicConfig};
/// use rust_remote_cw_keyer::sample::GpioState;
///
/// let mut keyer = IambicProcessor::new(IambicConfig::with_wpm(25));
///
/// // Simulate DIT paddle press
/// let mut gpio = GpioState::new();
/// gpio.set_dit(true);
///
/// let sample = keyer.tick(0, gpio);
/// assert!(sample.local_key); // Key down
/// ```
pub struct IambicProcessor {
    config: IambicConfig,

    // FSM state
    state: State,
    element_end_us: i64,
    last_element: Element,

    // Paddle state
    dit_pressed: bool,
    dah_pressed: bool,

    // Memory flags
    dit_memory: bool,
    dah_memory: bool,

    // Mode B: squeeze tracking
    squeeze_seen: bool,

    // Output state
    key_down: bool,
}

impl IambicProcessor {
    /// Create a new iambic processor with given configuration.
    pub fn new(config: IambicConfig) -> Self {
        Self {
            config,
            state: State::Idle,
            element_end_us: 0,
            last_element: Element::Dah, // Start with Dah so first press of Dit works
            dit_pressed: false,
            dah_pressed: false,
            dit_memory: false,
            dah_memory: false,
            squeeze_seen: false,
            key_down: false,
        }
    }

    /// Update configuration (e.g., change speed).
    pub fn set_config(&mut self, config: IambicConfig) {
        self.config = config;
    }

    /// Get current configuration.
    pub fn config(&self) -> &IambicConfig {
        &self.config
    }

    /// Tick the FSM and produce output sample.
    ///
    /// # Arguments
    ///
    /// * `now_us` - Current timestamp in microseconds
    /// * `gpio` - Current paddle GPIO state
    ///
    /// # Returns
    ///
    /// StreamSample with `local_key` set to current keying state.
    #[inline]
    pub fn tick(&mut self, now_us: i64, gpio: GpioState) -> StreamSample {
        // Update paddle state
        self.update_gpio(gpio);

        // Run FSM
        match self.state {
            State::Idle => self.tick_idle(now_us),
            State::SendDit => self.tick_sending(now_us, Element::Dit),
            State::SendDah => self.tick_sending(now_us, Element::Dah),
            State::Gap => self.tick_gap(now_us),
        }

        // Produce output sample
        StreamSample {
            gpio,
            local_key: self.key_down,
            audio_level: 0,  // Filled by audio envelope later
            flags: 0,
            config_gen: 0,   // Set by RT loop
        }
    }

    /// Check if key output is currently active.
    #[inline]
    pub fn is_key_down(&self) -> bool {
        self.key_down
    }

    /// Reset FSM to idle state.
    pub fn reset(&mut self) {
        self.state = State::Idle;
        self.element_end_us = 0;
        self.dit_memory = false;
        self.dah_memory = false;
        self.squeeze_seen = false;
        self.key_down = false;
    }

    // --- Private methods ---

    fn update_gpio(&mut self, gpio: GpioState) {
        let was_squeeze = self.dit_pressed && self.dah_pressed;

        self.dit_pressed = gpio.dit();
        self.dah_pressed = gpio.dah();

        let is_squeeze = self.dit_pressed && self.dah_pressed;

        // Track squeeze for Mode B
        if is_squeeze && !was_squeeze {
            self.squeeze_seen = true;
        }

        // Arm memory when paddle pressed during element/gap
        if self.state != State::Idle {
            if self.dit_pressed && self.config.dit_memory {
                self.dit_memory = true;
            }
            if self.dah_pressed && self.config.dah_memory {
                self.dah_memory = true;
            }
        }
    }

    fn tick_idle(&mut self, now_us: i64) {
        // Determine next element from memory or current paddle state
        let next = self.decide_next_element();

        if let Some(element) = next {
            self.start_element(element, now_us);
        }
    }

    fn tick_sending(&mut self, now_us: i64, element: Element) {
        if now_us >= self.element_end_us {
            // Element complete
            self.key_down = false;
            self.last_element = element;

            // Enter gap
            self.state = State::Gap;
            self.element_end_us = now_us + self.config.gap_duration_us();
        }
    }

    fn tick_gap(&mut self, now_us: i64) {
        if now_us >= self.element_end_us {
            // Gap complete
            self.state = State::Idle;

            // Immediately check for next element
            self.tick_idle(now_us);
        }
    }

    fn decide_next_element(&mut self) -> Option<Element> {
        // Priority 1: Memory (armed during previous element)
        if self.dit_memory {
            self.dit_memory = false;
            return Some(Element::Dit);
        }
        if self.dah_memory {
            self.dah_memory = false;
            return Some(Element::Dah);
        }

        // Priority 2: Mode B bonus element
        if self.config.mode == IambicMode::B && self.squeeze_seen {
            // Check if squeeze was released
            if !(self.dit_pressed && self.dah_pressed) {
                self.squeeze_seen = false;
                return Some(self.last_element.opposite());
            }
        }

        // Priority 3: Current paddle state
        match (self.dit_pressed, self.dah_pressed) {
            (true, true) => {
                // Squeeze: alternate from last element
                Some(self.last_element.opposite())
            }
            (true, false) => Some(Element::Dit),
            (false, true) => Some(Element::Dah),
            (false, false) => {
                // No paddle pressed
                self.squeeze_seen = false;
                None
            }
        }
    }

    fn start_element(&mut self, element: Element, now_us: i64) {
        self.key_down = true;
        self.squeeze_seen = self.dit_pressed && self.dah_pressed;

        let duration = match element {
            Element::Dit => self.config.dit_duration_us(),
            Element::Dah => self.config.dah_duration_us(),
        };

        self.state = match element {
            Element::Dit => State::SendDit,
            Element::Dah => State::SendDah,
        };

        self.element_end_us = now_us + duration;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_single_dit() {
        let config = IambicConfig::with_wpm(20);
        let dit_us = config.dit_duration_us();
        let gap_us = config.gap_duration_us();

        let mut keyer = IambicProcessor::new(config);

        // Press DIT
        let mut gpio = GpioState::new();
        gpio.set_dit(true);

        // Tick 0: should start dit
        let sample = keyer.tick(0, gpio);
        assert!(sample.local_key);

        // Mid-dit: still on
        let sample = keyer.tick(dit_us / 2, gpio);
        assert!(sample.local_key);

        // Dit complete: key off, in gap
        let sample = keyer.tick(dit_us, gpio);
        assert!(!sample.local_key);

        // Release paddle during gap
        gpio.set_dit(false);
        let sample = keyer.tick(dit_us + gap_us / 2, gpio);
        assert!(!sample.local_key);

        // Gap complete, no paddle: should stay idle
        let sample = keyer.tick(dit_us + gap_us + 1000, gpio);
        assert!(!sample.local_key);
    }

    #[test]
    fn test_squeeze_alternates() {
        let config = IambicConfig::with_wpm(20);
        let dit_us = config.dit_duration_us();
        let dah_us = config.dah_duration_us();
        let gap_us = config.gap_duration_us();

        let mut keyer = IambicProcessor::new(config);

        // Press both (squeeze)
        let mut gpio = GpioState::new();
        gpio.set_dit(true);
        gpio.set_dah(true);

        // Should start with Dit (opposite of initial last_element=Dah)
        let sample = keyer.tick(0, gpio);
        assert!(sample.local_key);

        // Complete dit + gap
        let t = dit_us + gap_us;
        let sample = keyer.tick(t, gpio);

        // Should now be sending Dah
        assert!(sample.local_key);

        // Complete dah + gap
        let t = t + dah_us + gap_us;
        let sample = keyer.tick(t, gpio);

        // Should be sending Dit again
        assert!(sample.local_key);
    }

    #[test]
    fn test_mode_b_bonus_element() {
        let config = IambicConfig {
            wpm: 20,
            mode: IambicMode::B,
            ..Default::default()
        };
        let dit_us = config.dit_duration_us();
        let gap_us = config.gap_duration_us();

        let mut keyer = IambicProcessor::new(config);

        // Press both (squeeze)
        let mut gpio = GpioState::new();
        gpio.set_dit(true);
        gpio.set_dah(true);

        // Start dit
        keyer.tick(0, gpio);

        // Release during dit
        gpio.set_dit(false);
        gpio.set_dah(false);
        keyer.tick(dit_us / 2, gpio);

        // Complete dit + gap
        let t = dit_us + gap_us;
        let sample = keyer.tick(t, gpio);

        // Mode B: should send bonus dah even though released
        assert!(sample.local_key);
    }

    #[test]
    fn test_dit_timing_accuracy() {
        let config = IambicConfig::with_wpm(25);
        let expected_dit_us = 48_000; // 1.2s / 25 = 48ms

        assert_eq!(config.dit_duration_us(), expected_dit_us);
    }

    #[test]
    fn test_memory() {
        let config = IambicConfig::with_wpm(20);
        let dit_us = config.dit_duration_us();
        let dah_us = config.dah_duration_us();
        let gap_us = config.gap_duration_us();

        let mut keyer = IambicProcessor::new(config);

        // Start with dah only
        let mut gpio = GpioState::new();
        gpio.set_dah(true);

        keyer.tick(0, gpio);
        assert!(keyer.is_key_down());

        // During dah, tap dit (memory)
        gpio.set_dit(true);
        keyer.tick(dah_us / 2, gpio);
        gpio.set_dit(false);
        keyer.tick(dah_us / 2 + 1000, gpio);

        // Release dah
        gpio.set_dah(false);
        keyer.tick(dah_us - 1000, gpio);

        // Complete dah + gap
        let t = dah_us + gap_us;
        let sample = keyer.tick(t, gpio);

        // Should send memorized dit
        assert!(sample.local_key);
    }
}
