//! Fault state management for RustRemoteCWKeyer.
//!
//! # Philosophy (from ARCHITECTURE.md §8)
//!
//! > Corrupted CW timing is worse than silence.
//! > If in doubt, FAULT and stop.
//!
//! A keyer that sends wrong timing is broken.
//! A keyer that sends nothing is safe.

use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};

/// Fault codes indicating why the keyer stopped.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum FaultCode {
    /// No fault (normal operation).
    None = 0,

    /// Stream buffer overrun: consumer fell too far behind.
    /// The consumer missed samples and timing is compromised.
    Overrun = 1,

    /// Latency exceeded: consumer is behind by more than allowed.
    /// Timing accuracy cannot be guaranteed.
    LatencyExceeded = 2,

    /// Producer overrun: writing faster than buffer can handle.
    /// Should not happen with correct sizing.
    ProducerOverrun = 3,

    /// Hardware fault: GPIO or peripheral error.
    HardwareFault = 4,
}

impl FaultCode {
    /// Convert from raw u8 value.
    pub fn from_u8(value: u8) -> Self {
        match value {
            0 => FaultCode::None,
            1 => FaultCode::Overrun,
            2 => FaultCode::LatencyExceeded,
            3 => FaultCode::ProducerOverrun,
            4 => FaultCode::HardwareFault,
            _ => FaultCode::None,
        }
    }
}

/// Thread-safe fault state.
///
/// Used by Hard RT consumers to signal timing failures.
/// Checked by main loop to trigger fault handling.
///
/// # Usage
///
/// ```ignore
/// static FAULT: FaultState = FaultState::new();
///
/// // In RT consumer:
/// if lag > max_allowed {
///     FAULT.set(FaultCode::LatencyExceeded, lag);
///     return Err(FaultCode::LatencyExceeded);
/// }
///
/// // In main loop:
/// if FAULT.is_active() {
///     audio_off();
///     tx_off();
///     signal_fault_led();
/// }
/// ```
pub struct FaultState {
    /// True if fault is active.
    active: AtomicBool,

    /// Fault code (reason for fault).
    code: AtomicU8,

    /// Additional data (e.g., lag in samples, latency in µs).
    data: AtomicU32,

    /// Total fault count since boot (never cleared).
    count: AtomicU32,
}

impl FaultState {
    /// Create new fault state (no fault).
    pub const fn new() -> Self {
        Self {
            active: AtomicBool::new(false),
            code: AtomicU8::new(0),
            data: AtomicU32::new(0),
            count: AtomicU32::new(0),
        }
    }

    /// Set fault state.
    ///
    /// This atomically sets the fault as active with the given code and data.
    /// Increments the fault counter.
    #[inline]
    pub fn set(&self, code: FaultCode, data: u32) {
        self.code.store(code as u8, Ordering::Release);
        self.data.store(data, Ordering::Release);
        self.count.fetch_add(1, Ordering::Relaxed);
        self.active.store(true, Ordering::Release);
    }

    /// Check if fault is currently active.
    #[inline]
    pub fn is_active(&self) -> bool {
        self.active.load(Ordering::Acquire)
    }

    /// Get fault code (only meaningful if `is_active()` is true).
    #[inline]
    pub fn code(&self) -> FaultCode {
        FaultCode::from_u8(self.code.load(Ordering::Acquire))
    }

    /// Get fault data (meaning depends on fault code).
    #[inline]
    pub fn data(&self) -> u32 {
        self.data.load(Ordering::Acquire)
    }

    /// Get total fault count since boot.
    #[inline]
    pub fn count(&self) -> u32 {
        self.count.load(Ordering::Relaxed)
    }

    /// Clear fault state (after recovery).
    ///
    /// Note: This clears the active flag but does NOT reset the counter.
    /// Fault history is preserved for diagnostics.
    #[inline]
    pub fn clear(&self) {
        self.active.store(false, Ordering::Release);
    }

    /// Get a snapshot of the current fault state.
    #[inline]
    pub fn snapshot(&self) -> FaultSnapshot {
        FaultSnapshot {
            active: self.is_active(),
            code: self.code(),
            data: self.data(),
            count: self.count(),
        }
    }
}

impl Default for FaultState {
    fn default() -> Self {
        Self::new()
    }
}

/// Snapshot of fault state at a point in time.
#[derive(Clone, Copy, Debug)]
pub struct FaultSnapshot {
    pub active: bool,
    pub code: FaultCode,
    pub data: u32,
    pub count: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fault_state_basic() {
        let fault = FaultState::new();

        assert!(!fault.is_active());
        assert_eq!(fault.code(), FaultCode::None);
        assert_eq!(fault.count(), 0);

        fault.set(FaultCode::LatencyExceeded, 42);

        assert!(fault.is_active());
        assert_eq!(fault.code(), FaultCode::LatencyExceeded);
        assert_eq!(fault.data(), 42);
        assert_eq!(fault.count(), 1);

        fault.clear();

        assert!(!fault.is_active());
        assert_eq!(fault.count(), 1); // Count preserved
    }

    #[test]
    fn test_fault_count_accumulates() {
        let fault = FaultState::new();

        fault.set(FaultCode::Overrun, 1);
        fault.clear();
        fault.set(FaultCode::LatencyExceeded, 2);
        fault.clear();
        fault.set(FaultCode::Overrun, 3);

        assert_eq!(fault.count(), 3);
    }
}
