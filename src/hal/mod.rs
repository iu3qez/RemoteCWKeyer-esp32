//! Hardware Abstraction Layer for RustRemoteCWKeyer.
//!
//! Thin wrappers around ESP-IDF peripherals.
//! Business logic stays in core modules, HAL is just I/O.

pub mod gpio;
pub mod audio;
