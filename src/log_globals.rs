//! Global log stream instances.
//!
//! Dual log streams following ARCHITECTURE.md §11.1.4:
//! "single producer per core, single consumer"

use crate::logging::LogStream;

/// RT log stream for Core 0 (RT thread only).
///
/// This stream receives logs from the real-time thread running on Core 0.
/// Single producer (RT thread), single consumer (UART drain).
pub static RT_LOG_STREAM: LogStream = LogStream::new();

/// Background log stream for Core 1 (best-effort threads).
///
/// This stream receives logs from multiple best-effort threads on Core 1.
/// Multiple producers (coordinated via SPMC), single consumer (UART drain).
pub static BG_LOG_STREAM: LogStream = LogStream::new();
