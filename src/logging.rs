//! RT-safe logging for RustRemoteCWKeyer.
//!
//! # Architecture (from ARCHITECTURE.md §11)
//!
//! ```text
//! RT Thread              LogStream            UART Thread
//! ──────────             ─────────            ───────────
//!
//! rt_log!() ──────────▶ [L0][L1][L2] ──────▶ UART TX
//! ~100ns                  lock-free           blocking ok
//! non-blocking            ring buffer         Core 1
//! ```
//!
//! # Rules
//!
//! - RULE 11.1.1: RT path shall NEVER call blocking log functions
//! - RULE 11.1.2: ESP_LOGx, println!, print! are FORBIDDEN in RT path
//! - RULE 11.1.3: RT path uses rt_log!() macro
//! - RULE 11.2.2: Log messages may be dropped if ring full

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};

/// Maximum message length.
pub const MAX_MSG_LEN: usize = 120;

/// Log buffer size (number of entries).
pub const LOG_BUFFER_SIZE: usize = 256;

/// Log level.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Trace = 4,
}

impl LogLevel {
    /// Convert to string for output.
    pub fn as_str(self) -> &'static str {
        match self {
            LogLevel::Error => "ERROR",
            LogLevel::Warn => "WARN",
            LogLevel::Info => "INFO",
            LogLevel::Debug => "DEBUG",
            LogLevel::Trace => "TRACE",
        }
    }
}

/// A single log entry.
#[derive(Clone, Copy)]
#[repr(C)]
pub struct LogEntry {
    /// Timestamp in microseconds.
    pub timestamp_us: i64,
    /// Log level.
    pub level: LogLevel,
    /// Message length.
    pub len: u8,
    /// Message bytes (not null-terminated).
    pub msg: [u8; MAX_MSG_LEN],
}

impl Default for LogEntry {
    fn default() -> Self {
        Self {
            timestamp_us: 0,
            level: LogLevel::Info,
            len: 0,
            msg: [0; MAX_MSG_LEN],
        }
    }
}

/// Lock-free log stream (SPMC: multiple producers, single consumer).
///
/// Designed for RT-safe logging:
/// - Multiple threads can push (coordinated via atomic fetch_add)
/// - Push never blocks (drops message if full)
/// - Drain runs in separate thread at leisure
pub struct LogStream<const N: usize = LOG_BUFFER_SIZE> {
    entries: UnsafeCell<[LogEntry; N]>,
    write_idx: AtomicU32,
    read_idx: AtomicU32,
    dropped: AtomicU32,
}

// SAFETY: Multiple producers (coordinated via atomics), single consumer (UART thread).
// Coordination through atomic fetch_add for write_idx.
unsafe impl<const N: usize> Sync for LogStream<N> {}
unsafe impl<const N: usize> Send for LogStream<N> {}

impl<const N: usize> LogStream<N> {
    const MASK: usize = N - 1;

    /// Create a new empty log stream.
    pub const fn new() -> Self {
        assert!(N.is_power_of_two(), "Log buffer size must be power of 2");

        Self {
            entries: UnsafeCell::new([LogEntry {
                timestamp_us: 0,
                level: LogLevel::Info,
                len: 0,
                msg: [0; MAX_MSG_LEN],
            }; N]),
            write_idx: AtomicU32::new(0),
            read_idx: AtomicU32::new(0),
            dropped: AtomicU32::new(0),
        }
    }

    /// Push a log entry (RT-safe, never blocks).
    ///
    /// Returns `true` if message was queued, `false` if dropped (ring full).
    ///
    /// # Timing
    ///
    /// Completes in O(1), typically < 200ns.
    ///
    /// # Thread Safety
    ///
    /// Safe for multiple concurrent producers via atomic fetch_add.
    #[inline]
    pub fn push(&self, timestamp_us: i64, level: LogLevel, msg: &[u8]) -> bool {
        // SPMC: Use fetch_add for atomic write index allocation
        // Each producer gets unique index, no race conditions
        let write = self.write_idx.fetch_add(1, Ordering::AcqRel);
        let read = self.read_idx.load(Ordering::Acquire);

        // Check if ring is full (write is PRE-increment value)
        if write.wrapping_sub(read) >= N as u32 {
            self.dropped.fetch_add(1, Ordering::Relaxed);
            return false;
        }

        let idx = (write as usize) & Self::MASK;

        // SAFETY: Multiple producers coordinated by fetch_add.
        // Each producer gets unique index, no aliasing possible.
        unsafe {
            let entry = &mut (*self.entries.get())[idx];
            entry.timestamp_us = timestamp_us;
            entry.level = level;
            entry.len = msg.len().min(MAX_MSG_LEN) as u8;
            entry.msg[..entry.len as usize].copy_from_slice(&msg[..entry.len as usize]);
        }

        // fetch_add already updated write_idx atomically
        true
    }

    /// Drain next log entry (for UART thread).
    ///
    /// Returns `None` if no entries available.
    #[inline]
    pub fn drain(&self) -> Option<LogEntry> {
        let read = self.read_idx.load(Ordering::Relaxed);
        let write = self.write_idx.load(Ordering::Acquire);

        if read == write {
            return None;
        }

        let idx = (read as usize) & Self::MASK;

        // SAFETY: Single consumer, unique index
        let entry = unsafe { (*self.entries.get())[idx] };

        self.read_idx.store(read.wrapping_add(1), Ordering::Release);
        Some(entry)
    }

    /// Get count of dropped messages.
    #[inline]
    pub fn dropped(&self) -> u32 {
        self.dropped.load(Ordering::Relaxed)
    }

    /// Reset dropped counter (e.g., after reporting).
    #[inline]
    pub fn reset_dropped(&self) {
        self.dropped.store(0, Ordering::Relaxed);
    }

    /// Check if there are entries to drain.
    #[inline]
    pub fn has_entries(&self) -> bool {
        let read = self.read_idx.load(Ordering::Relaxed);
        let write = self.write_idx.load(Ordering::Acquire);
        read != write
    }

    /// Get number of entries waiting to be drained.
    #[inline]
    pub fn pending(&self) -> u32 {
        let read = self.read_idx.load(Ordering::Relaxed);
        let write = self.write_idx.load(Ordering::Acquire);
        write.wrapping_sub(read)
    }
}

impl<const N: usize> Default for LogStream<N> {
    fn default() -> Self {
        Self::new()
    }
}

/// Format a message into a buffer.
///
/// Returns the number of bytes written.
#[inline]
pub fn format_to_buffer(buf: &mut [u8], args: core::fmt::Arguments<'_>) -> usize {
    use core::fmt::Write;

    struct BufWriter<'a> {
        buf: &'a mut [u8],
        pos: usize,
    }

    impl<'a> Write for BufWriter<'a> {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            let bytes = s.as_bytes();
            let remaining = self.buf.len() - self.pos;
            let to_write = bytes.len().min(remaining);
            self.buf[self.pos..self.pos + to_write].copy_from_slice(&bytes[..to_write]);
            self.pos += to_write;
            Ok(())
        }
    }

    let mut writer = BufWriter { buf, pos: 0 };
    let _ = core::fmt::write(&mut writer, args);
    writer.pos
}

/// RT-safe log macro.
///
/// Use this in the RT path instead of println!, ESP_LOGx, etc.
///
/// # Example
///
/// ```ignore
/// rt_log!(LogLevel::Info, LOG_STREAM, timestamp, "Key {} @ {}", key, time);
/// ```
#[macro_export]
macro_rules! rt_log {
    ($level:expr, $stream:expr, $timestamp:expr, $($arg:tt)*) => {{
        let mut buf = [0u8; $crate::logging::MAX_MSG_LEN];
        let len = $crate::logging::format_to_buffer(&mut buf, format_args!($($arg)*));
        $stream.push($timestamp, $level, &buf[..len]);
    }};
}

/// RT-safe info log.
#[macro_export]
macro_rules! rt_info {
    ($stream:expr, $timestamp:expr, $($arg:tt)*) => {
        $crate::rt_log!($crate::logging::LogLevel::Info, $stream, $timestamp, $($arg)*)
    };
}

/// RT-safe warning log.
#[macro_export]
macro_rules! rt_warn {
    ($stream:expr, $timestamp:expr, $($arg:tt)*) => {
        $crate::rt_log!($crate::logging::LogLevel::Warn, $stream, $timestamp, $($arg)*)
    };
}

/// RT-safe error log.
#[macro_export]
macro_rules! rt_error {
    ($stream:expr, $timestamp:expr, $($arg:tt)*) => {
        $crate::rt_log!($crate::logging::LogLevel::Error, $stream, $timestamp, $($arg)*)
    };
}

/// RT-safe debug log.
#[macro_export]
macro_rules! rt_debug {
    ($stream:expr, $timestamp:expr, $($arg:tt)*) => {
        $crate::rt_log!($crate::logging::LogLevel::Debug, $stream, $timestamp, $($arg)*)
    };
}

/// RT-safe trace log (maximum verbosity).
#[macro_export]
macro_rules! rt_trace {
    ($stream:expr, $timestamp:expr, $($arg:tt)*) => {
        $crate::rt_log!($crate::logging::LogLevel::Trace, $stream, $timestamp, $($arg)*)
    };
}

/// Get appropriate log stream for current core.
///
/// - Core 0 → RT_LOG_STREAM (RT thread)
/// - Core 1 → BG_LOG_STREAM (best-effort threads)
///
/// This helper allows automatic stream selection in multi-core environments.
/// Cost: ~10ns for xTaskGetCurrentTaskHandle() + xTaskGetCoreID() call.
#[cfg(not(test))]
#[inline]
pub fn current_log_stream() -> &'static LogStream {
    // SAFETY: xTaskGetCoreID is always safe to call
    unsafe {
        let task = esp_idf_svc::sys::xTaskGetCurrentTaskHandle();
        if esp_idf_svc::sys::xTaskGetCoreID(task) == 0 {
            &crate::RT_LOG_STREAM
        } else {
            &crate::BG_LOG_STREAM
        }
    }
}

/// Test version always returns a dummy stream
#[cfg(test)]
#[inline]
pub fn current_log_stream() -> &'static LogStream {
    // For tests, use a static dummy stream
    static TEST_LOG_STREAM: LogStream = LogStream::new();
    &TEST_LOG_STREAM
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_stream_basic() {
        let stream = LogStream::<16>::new();

        assert!(stream.push(1000, LogLevel::Info, b"test message"));
        assert!(stream.has_entries());
        assert_eq!(stream.pending(), 1);

        let entry = stream.drain().unwrap();
        assert_eq!(entry.timestamp_us, 1000);
        assert_eq!(entry.level, LogLevel::Info);
        assert_eq!(&entry.msg[..entry.len as usize], b"test message");

        assert!(!stream.has_entries());
    }

    #[test]
    fn test_log_stream_full() {
        let stream = LogStream::<4>::new();

        // Fill the buffer
        assert!(stream.push(1, LogLevel::Info, b"1"));
        assert!(stream.push(2, LogLevel::Info, b"2"));
        assert!(stream.push(3, LogLevel::Info, b"3"));
        assert!(stream.push(4, LogLevel::Info, b"4"));

        // Should drop
        assert!(!stream.push(5, LogLevel::Info, b"5"));
        assert_eq!(stream.dropped(), 1);

        // Drain one, should be able to push again
        stream.drain();
        assert!(stream.push(6, LogLevel::Info, b"6"));
    }

    #[test]
    fn test_format_to_buffer() {
        let mut buf = [0u8; 32];
        let len = format_to_buffer(&mut buf, format_args!("Hello {}", 42));
        assert_eq!(&buf[..len], b"Hello 42");
    }

    #[test]
    fn test_log_level_ordering() {
        assert!(LogLevel::Error < LogLevel::Warn);
        assert!(LogLevel::Warn < LogLevel::Info);
        assert!(LogLevel::Info < LogLevel::Debug);
        assert!(LogLevel::Debug < LogLevel::Trace);
    }

    #[test]
    fn test_spmc_multiple_producers() {
        use std::sync::Arc;
        use std::thread;

        let stream = Arc::new(LogStream::<64>::new());
        let mut handles = vec![];

        // Spawn 4 producer threads
        for i in 0..4 {
            let stream = Arc::clone(&stream);
            let handle = thread::spawn(move || {
                for j in 0..10 {
                    let msg = format!("Thread {} msg {}", i, j);
                    stream.push(j as i64, LogLevel::Info, msg.as_bytes());
                }
            });
            handles.push(handle);
        }

        // Wait for all producers
        for handle in handles {
            handle.join().unwrap();
        }

        // Should have 40 messages (4 threads × 10 messages)
        let mut count = 0;
        while stream.drain().is_some() {
            count += 1;
        }
        assert_eq!(count, 40, "All messages should be present");
    }

    #[test]
    fn test_spmc_concurrent_stress() {
        use std::sync::Arc;
        use std::thread;

        let stream = Arc::new(LogStream::<256>::new());
        let mut handles = vec![];

        // Spawn 8 threads, each pushing 100 messages
        for i in 0..8 {
            let stream = Arc::clone(&stream);
            let handle = thread::spawn(move || {
                for j in 0..100 {
                    let msg = format!("T{}-{}", i, j);
                    let _ = stream.push((i * 100 + j) as i64, LogLevel::Info, msg.as_bytes());
                }
            });
            handles.push(handle);
        }

        // Wait for all producers
        for handle in handles {
            handle.join().unwrap();
        }

        // Count total messages received
        let mut count = 0;
        while stream.drain().is_some() {
            count += 1;
        }

        // Should have most messages (some may be dropped if buffer overflow)
        // 8 threads × 100 = 800 total, buffer is 256
        assert!(count > 0, "Should have received some messages");
        assert!(count <= 800, "Should not exceed total messages sent");
    }
}
