//! GPIO HAL for paddle input and TX output.

// TODO: Implement with esp-idf-hal::gpio

/// Paddle pin configuration.
pub struct PaddleConfig {
    pub dit_pin: i32,
    pub dah_pin: i32,
    pub straight_key_pin: Option<i32>,
    pub active_low: bool,
}

/// TX output configuration.
pub struct TxConfig {
    pub pin: i32,
    pub active_high: bool,
}
