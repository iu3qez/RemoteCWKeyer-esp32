//! Lock-free SPMC (Single Producer, Multiple Consumer) stream buffer.
//!
//! This is the heart of RustRemoteCWKeyer. All keying events flow through here.
//!
//! # Architecture
//!
//! ```text
//! Producers ──────▶ KeyingStream ──────▶ Consumers
//!                   (lock-free)
//!                   (single truth)
//! ```
//!
//! # Rules (from ARCHITECTURE.md)
//!
//! - RULE 1.1.1: All keying events flow through KeyingStream
//! - RULE 1.1.2: No component communicates except through the stream
//! - RULE 3.1.1: Only atomic operations for synchronization
//! - RULE 3.1.4: No operation shall block

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};

use crate::sample::{StreamSample, FLAG_SILENCE};

/// Default stream size: 4096 samples.
/// At 10kHz, this is ~400ms of buffer.
/// At 20kHz, this is ~200ms of buffer.
pub const DEFAULT_STREAM_SIZE: usize = 4096;

/// Lock-free SPMC ring buffer for keying events.
///
/// # Safety
///
/// This type uses `UnsafeCell` internally but is safe to use because:
/// - Single producer (enforced by design, not by type system)
/// - Multiple consumers each maintain their own read index
/// - All coordination through atomic operations
/// - No mutable aliasing possible within the rules
///
/// # Memory Ordering
///
/// - Producer uses `AcqRel` for `write_idx.fetch_add()`
/// - Consumer uses `Acquire` for `write_idx.load()`
/// - This ensures consumers see all writes before the index update
pub struct KeyingStream<const N: usize = DEFAULT_STREAM_SIZE> {
    /// Ring buffer of samples.
    slots: UnsafeCell<[StreamSample; N]>,

    /// Next write index (monotonically increasing, wraps via mask).
    write_idx: AtomicU32,

    /// Idle tick counter for silence compression.
    idle_ticks: AtomicU32,

    /// Last sample written (for change detection).
    last_sample: UnsafeCell<StreamSample>,
}

// SAFETY: Single producer, multiple consumers, atomic coordination.
// No mutable aliasing possible within the architectural rules.
unsafe impl<const N: usize> Sync for KeyingStream<N> {}
unsafe impl<const N: usize> Send for KeyingStream<N> {}

impl<const N: usize> KeyingStream<N> {
    /// Mask for wrapping index to buffer size.
    /// N must be a power of 2.
    const MASK: usize = N - 1;

    /// Create a new empty stream.
    ///
    /// # Panics
    ///
    /// Panics at compile time if N is not a power of 2.
    pub const fn new() -> Self {
        // Compile-time check: N must be power of 2
        assert!(N.is_power_of_two(), "Stream size must be power of 2");

        Self {
            slots: UnsafeCell::new([StreamSample::EMPTY; N]),
            write_idx: AtomicU32::new(0),
            idle_ticks: AtomicU32::new(0),
            last_sample: UnsafeCell::new(StreamSample::EMPTY),
        }
    }

    /// Push a sample to the stream.
    ///
    /// If the sample is unchanged from the previous, increments idle counter
    /// instead of writing (silence compression).
    ///
    /// # Timing
    ///
    /// This operation completes in O(1) time, typically < 200ns.
    /// Never blocks, never allocates.
    #[inline]
    pub fn push(&self, sample: StreamSample) {
        // SAFETY: Single producer, no aliasing
        let last = unsafe { &*self.last_sample.get() };

        if sample.has_change_from(last) {
            // State changed: flush accumulated idle ticks, then write sample

            let idle = self.idle_ticks.swap(0, Ordering::Relaxed);
            if idle > 0 {
                self.write_slot(StreamSample::silence(idle));
            }

            // Write sample with edge flags
            let sample_with_edges = sample.with_edges_from(last);
            self.write_slot(sample_with_edges);

            // SAFETY: Single producer, no aliasing
            unsafe {
                *self.last_sample.get() = sample;
            }
        } else {
            // No change: accumulate idle ticks (silence compression)
            self.idle_ticks.fetch_add(1, Ordering::Relaxed);
        }
    }

    /// Push a sample unconditionally (no silence compression).
    ///
    /// Use this when you need every sample recorded, regardless of changes.
    #[inline]
    pub fn push_raw(&self, sample: StreamSample) {
        self.write_slot(sample);
    }

    /// Write a slot to the ring buffer.
    #[inline]
    fn write_slot(&self, sample: StreamSample) {
        // RULE 3.1.2: AcqRel for read-modify-write
        let idx = self.write_idx.fetch_add(1, Ordering::AcqRel) as usize;

        // SAFETY: Single producer, index is unique
        unsafe {
            (*self.slots.get())[idx & Self::MASK] = sample;
        }
    }

    /// Flush any accumulated idle ticks as a silence marker.
    ///
    /// Call this before shutting down to ensure all state is captured.
    #[inline]
    pub fn flush(&self) {
        let idle = self.idle_ticks.swap(0, Ordering::Relaxed);
        if idle > 0 {
            self.write_slot(StreamSample::silence(idle));
        }
    }

    /// Read a sample at the given index.
    ///
    /// Returns `None` if:
    /// - Index is ahead of write head (not yet written)
    /// - Index is too far behind (overwritten)
    ///
    /// # Timing
    ///
    /// This operation completes in O(1) time, typically < 50ns.
    #[inline]
    pub fn read(&self, idx: u32) -> Option<StreamSample> {
        // RULE 3.1.3: Acquire for read
        let write = self.write_idx.load(Ordering::Acquire);
        let behind = write.wrapping_sub(idx);

        if behind == 0 {
            // Not yet written
            return None;
        }
        if behind > N as u32 {
            // Overwritten (consumer too slow)
            return None;
        }

        // SAFETY: Index is valid, single producer guarantees no concurrent write
        Some(unsafe { (*self.slots.get())[(idx as usize) & Self::MASK] })
    }

    /// Get the current write head index.
    ///
    /// Consumers use this to initialize their read index.
    #[inline]
    pub fn write_head(&self) -> u32 {
        self.write_idx.load(Ordering::Acquire)
    }

    /// Calculate how many samples behind a consumer is.
    #[inline]
    pub fn lag(&self, reader_idx: u32) -> u32 {
        self.write_idx.load(Ordering::Acquire).wrapping_sub(reader_idx)
    }

    /// Check if a consumer has fallen too far behind (overrun).
    ///
    /// If true, the consumer has missed samples and should resync.
    #[inline]
    pub fn is_overrun(&self, reader_idx: u32) -> bool {
        self.lag(reader_idx) > N as u32
    }

    /// Get the buffer capacity.
    #[inline]
    pub const fn capacity(&self) -> usize {
        N
    }
}

impl<const N: usize> Default for KeyingStream<N> {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sample::GpioState;

    #[test]
    fn test_stream_basic_write_read() {
        let stream = KeyingStream::<64>::new();

        let mut sample = StreamSample::EMPTY;
        sample.local_key = true;
        stream.push(sample);

        let read = stream.read(0);
        assert!(read.is_some());
        assert!(read.unwrap().local_key);
    }

    #[test]
    fn test_stream_silence_compression() {
        let stream = KeyingStream::<64>::new();

        // Push same sample multiple times
        let sample = StreamSample::EMPTY;
        for _ in 0..100 {
            stream.push(sample);
        }

        // Flush to write silence marker
        stream.flush();

        // Should have written only 1 silence marker
        assert_eq!(stream.write_head(), 1);

        let read = stream.read(0).unwrap();
        assert!(read.is_silence());
        assert_eq!(read.silence_ticks(), 100);
    }

    #[test]
    fn test_stream_edge_detection() {
        let stream = KeyingStream::<64>::new();

        // First sample: key up
        let mut s1 = StreamSample::EMPTY;
        s1.local_key = false;
        stream.push(s1);

        // Second sample: key down
        let mut s2 = StreamSample::EMPTY;
        s2.local_key = true;
        stream.push(s2);

        // Read second sample, should have edge flag
        let read = stream.read(1).unwrap();
        assert!(read.has_local_edge());
        assert!(read.local_key);
    }

    #[test]
    fn test_stream_overrun_detection() {
        let stream = KeyingStream::<64>::new();

        // Write more than buffer size
        for i in 0..100 {
            let mut sample = StreamSample::EMPTY;
            sample.gpio = GpioState { bits: i as u8 };
            stream.push_raw(sample);
        }

        // Old indices should be overrun
        assert!(stream.is_overrun(0));
        assert!(stream.is_overrun(30));

        // Recent indices should be valid
        assert!(!stream.is_overrun(50));
        assert!(!stream.is_overrun(99));
    }

    #[test]
    fn test_stream_lag_calculation() {
        let stream = KeyingStream::<64>::new();

        for _ in 0..10 {
            stream.push_raw(StreamSample::EMPTY);
        }

        assert_eq!(stream.lag(0), 10);
        assert_eq!(stream.lag(5), 5);
        assert_eq!(stream.lag(10), 0);
    }
}
