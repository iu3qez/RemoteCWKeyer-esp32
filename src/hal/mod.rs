//! Hardware Abstraction Layer for RustRemoteCWKeyer.
//!
//! Thin wrappers around ESP-IDF peripherals.
//! Business logic stays in core modules, HAL is just I/O.

pub mod gpio;
pub mod audio;
pub mod es8311;

pub use es8311::{Es8311, Es8311Config, Es8311Error, ES8311_ADDR};
