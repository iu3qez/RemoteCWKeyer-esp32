//! # RustRemoteCWKeyer
//!
//! Professional CW keyer with lock-free SPMC architecture.
//!
//! ## Architecture
//!
//! All communication flows through [`KeyingStream`]. Components are isolated:
//! - Producers write to stream, don't know who reads
//! - Consumers read from stream, don't know who writes
//! - No callbacks, no shared state, no mutexes
//!
//! See ARCHITECTURE.md for immutable design principles.

#![cfg_attr(not(test), no_std)]

pub mod config;
pub mod sample;
pub mod stream;
pub mod consumer;
pub mod iambic;
pub mod logging;
pub mod fault;

pub use config::CONFIG;
pub use sample::{StreamSample, GpioState};
pub use stream::KeyingStream;
pub use consumer::{HardRtConsumer, BestEffortConsumer};
pub use fault::{FaultState, FaultCode};
pub use iambic::IambicProcessor;
