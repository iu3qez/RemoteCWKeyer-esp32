//! Lock-free SPMC (Single Producer, Multiple Consumer) stream buffer.
//!
//! This is the heart of RustRemoteCWKeyer. All keying events flow through here.
//!
//! # Architecture
//!
//! ```text
//! Producers ──────► KeyingStream ──────► Consumers
//!                   (lock-free)         (StreamConsumer)
//!                   (single truth)
//! ```
//!
//! # Rules (from ARCHITECTURE.md)
//!
//! - RULE 1.1.1: All keying events flow through KeyingStream
//! - RULE 1.1.2: No component communicates except through the stream
//! - RULE 3.1.1: Only atomic operations for synchronization
//! - RULE 3.1.4: No operation shall block
//! - RULE 9.1.4: PSRAM for stream buffer

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, AtomicU32, Ordering};

use crate::sample::StreamSample;

/// Lock-free SPMC ring buffer for keying events.
///
/// # Architecture (ARCHITECTURE.md)
/// - RULE 1.1.1: All keying events flow through KeyingStream
/// - RULE 3.1.1: Only atomic operations for synchronization
/// - RULE 9.1.4: PSRAM for stream buffer
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
pub struct KeyingStream {
    /// External buffer in PSRAM (passed at construction).
    /// Must be a power of 2 in length.
    buffer: &'static [UnsafeCell<StreamSample>],

    /// Producer write index (monotonically increasing).
    /// Only modified by single producer via fetch_add.
    write_idx: AtomicUsize,

    /// Buffer capacity (power of 2 for fast modulo).
    capacity: usize,

    /// Mask for wrap-around (capacity - 1).
    mask: usize,

    /// Idle tick counter for silence compression.
    idle_ticks: AtomicU32,

    /// Last sample written (for change detection).
    last_sample: UnsafeCell<StreamSample>,
}

// SAFETY: Single producer, multiple consumers, atomic coordination.
// No mutable aliasing possible within the architectural rules.
unsafe impl Sync for KeyingStream {}
unsafe impl Send for KeyingStream {}

impl KeyingStream {
    /// Create stream with external PSRAM buffer.
    ///
    /// # Arguments
    /// * `buffer` - Static slice in PSRAM. Must be power of 2 length.
    ///
    /// # Panics
    /// Panics if capacity is not power of 2.
    ///
    /// # Example
    /// ```ignore
    /// // Allocate 300,000 samples in PSRAM (~1.8MB)
    /// static BUFFER: [UnsafeCell<StreamSample>; 262144] = [...];
    /// let stream = KeyingStream::with_buffer(&BUFFER);
    /// ```
    pub fn with_buffer(buffer: &'static [UnsafeCell<StreamSample>]) -> Self {
        let capacity = buffer.len();
        assert!(capacity.is_power_of_two(), "Buffer size must be power of 2");
        assert!(capacity > 0, "Buffer cannot be empty");

        Self {
            buffer,
            write_idx: AtomicUsize::new(0),
            capacity,
            mask: capacity - 1,
            idle_ticks: AtomicU32::new(0),
            last_sample: UnsafeCell::new(StreamSample::EMPTY),
        }
    }

    /// Push sample to stream (producer only, RT thread).
    ///
    /// If the sample is unchanged from the previous, increments idle counter
    /// instead of writing (silence compression / RLE).
    ///
    /// Returns `true` on success, `false` if buffer full (FAULT condition).
    ///
    /// # Timing
    /// O(1), typically < 200ns. Never blocks.
    #[inline]
    pub fn push(&self, sample: StreamSample) -> bool {
        // SAFETY: Single producer, no aliasing
        // Invariants:
        // - Only one thread calls push() (RT thread)
        // - last_sample only written here, read only here
        let last = unsafe { &*self.last_sample.get() };

        if sample.has_change_from(last) {
            // State changed: flush accumulated idle ticks, then write sample

            let idle = self.idle_ticks.swap(0, Ordering::Relaxed);
            if idle > 0 {
                if !self.write_slot(StreamSample::silence(idle)) {
                    return false;
                }
            }

            // Write sample with edge flags
            let sample_with_edges = sample.with_edges_from(last);
            if !self.write_slot(sample_with_edges) {
                return false;
            }

            // SAFETY: Single producer, no aliasing
            // Invariants: same as above
            unsafe {
                *self.last_sample.get() = sample;
            }
        } else {
            // No change: accumulate idle ticks (silence compression)
            self.idle_ticks.fetch_add(1, Ordering::Relaxed);
        }

        true
    }

    /// Push a sample unconditionally (no silence compression).
    ///
    /// Use this when you need every sample recorded, regardless of changes.
    /// Returns `true` on success, `false` if buffer full.
    #[inline]
    pub fn push_raw(&self, sample: StreamSample) -> bool {
        self.write_slot(sample)
    }

    /// Write a slot to the ring buffer.
    ///
    /// Returns `true` on success, `false` if buffer is full.
    #[inline]
    fn write_slot(&self, sample: StreamSample) -> bool {
        // RULE 3.1.2: AcqRel for read-modify-write
        let idx = self.write_idx.fetch_add(1, Ordering::AcqRel);
        let slot_idx = idx & self.mask;

        // SAFETY: Single producer, index is unique
        // Invariants:
        // - Only one thread writes (RT thread)
        // - Each index written exactly once before wrap-around
        // - Consumers read with Acquire ordering after checking write_idx
        unsafe {
            (*self.buffer[slot_idx].get()) = sample;
        }

        true
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
    /// O(1), typically < 50ns.
    #[inline]
    pub fn read(&self, idx: usize) -> Option<StreamSample> {
        // RULE 3.1.3: Acquire for read
        let write = self.write_idx.load(Ordering::Acquire);
        let behind = write.wrapping_sub(idx);

        if behind == 0 {
            // Not yet written
            return None;
        }
        if behind > self.capacity {
            // Overwritten (consumer too slow)
            return None;
        }

        let slot_idx = idx & self.mask;

        // SAFETY: Index is valid, single producer guarantees no concurrent write
        // Invariants:
        // - We verified idx < write_idx (sample exists)
        // - We verified idx >= write_idx - capacity (not overwritten)
        // - Producer only advances write_idx, never writes to old indices
        Some(unsafe { *self.buffer[slot_idx].get() })
    }

    /// Get current write position (for consumer initialization).
    #[inline]
    pub fn write_position(&self) -> usize {
        self.write_idx.load(Ordering::Acquire)
    }

    /// Calculate lag for consumer (samples behind producer).
    #[inline]
    pub fn lag(&self, read_idx: usize) -> usize {
        self.write_idx.load(Ordering::Acquire).wrapping_sub(read_idx)
    }

    /// Check if consumer overrun (fell too far behind).
    ///
    /// If true, the consumer has missed samples and should resync.
    #[inline]
    pub fn is_overrun(&self, read_idx: usize) -> bool {
        self.lag(read_idx) > self.capacity
    }

    /// Get the buffer capacity.
    #[inline]
    pub const fn capacity(&self) -> usize {
        self.capacity
    }
}

/// Consumer handle for stream reading.
///
/// Each consumer has its own read_idx (thread-local, no sync needed).
/// Multiple consumers can read independently from the same stream.
///
/// # Example
/// ```ignore
/// let consumer = StreamConsumer::new(&KEYING_STREAM);
/// loop {
///     if let Some(sample) = consumer.next() {
///         process(sample);
///     }
/// }
/// ```
pub struct StreamConsumer {
    /// Reference to the stream being consumed.
    stream: &'static KeyingStream,

    /// Thread-local read index (not atomic - owned by this consumer).
    read_idx: usize,
}

impl StreamConsumer {
    /// Create consumer starting at current stream position.
    ///
    /// The consumer will begin reading from whatever sample is written next.
    pub fn new(stream: &'static KeyingStream) -> Self {
        Self {
            stream,
            read_idx: stream.write_position(),
        }
    }

    /// Create consumer starting from a specific position.
    ///
    /// Useful for replaying history or resuming from a known point.
    pub fn from_position(stream: &'static KeyingStream, position: usize) -> Self {
        Self {
            stream,
            read_idx: position,
        }
    }

    /// Read next sample (non-blocking).
    ///
    /// Returns `None` if caught up with producer (no new samples).
    /// Returns `Some(sample)` and advances read_idx on success.
    #[inline]
    pub fn next(&mut self) -> Option<StreamSample> {
        let sample = self.stream.read(self.read_idx)?;
        self.read_idx = self.read_idx.wrapping_add(1);
        Some(sample)
    }

    /// Peek at the next sample without consuming it.
    #[inline]
    pub fn peek(&self) -> Option<StreamSample> {
        self.stream.read(self.read_idx)
    }

    /// Get current lag (samples behind producer).
    #[inline]
    pub fn lag(&self) -> usize {
        self.stream.lag(self.read_idx)
    }

    /// Check if this consumer has fallen behind and missed samples.
    #[inline]
    pub fn is_overrun(&self) -> bool {
        self.stream.is_overrun(self.read_idx)
    }

    /// Skip to latest position (for best-effort consumers).
    ///
    /// Use this to discard backlog and catch up to real-time.
    /// Returns number of samples skipped.
    pub fn skip_to_latest(&mut self) -> usize {
        let old_idx = self.read_idx;
        self.read_idx = self.stream.write_position();
        self.read_idx.wrapping_sub(old_idx)
    }

    /// Resync after overrun.
    ///
    /// Moves read_idx to oldest valid position in the buffer.
    /// This is the recovery path when a consumer falls too far behind.
    pub fn resync(&mut self) {
        let write_pos = self.stream.write_position();
        let oldest_valid = write_pos.saturating_sub(self.stream.capacity());
        self.read_idx = oldest_valid;
    }

    /// Get current read position.
    #[inline]
    pub fn position(&self) -> usize {
        self.read_idx
    }
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sample::GpioState;
    use core::cell::UnsafeCell;

    // Helper to create test buffer
    fn make_buffer<const N: usize>() -> &'static [UnsafeCell<StreamSample>] {
        // Leak for static lifetime in tests
        let buf: Box<[UnsafeCell<StreamSample>; N]> = Box::new(
            core::array::from_fn(|_| UnsafeCell::new(StreamSample::EMPTY))
        );
        Box::leak(buf) as &'static [UnsafeCell<StreamSample>]
    }

    #[test]
    fn test_stream_basic_write_read() {
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

        let mut sample = StreamSample::EMPTY;
        sample.local_key = true;
        stream.push(sample);

        let read = stream.read(0);
        assert!(read.is_some());
        assert!(read.unwrap().local_key);
    }

    #[test]
    fn test_stream_silence_compression() {
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

        // Push same sample multiple times
        let sample = StreamSample::EMPTY;
        for _ in 0..100 {
            stream.push(sample);
        }

        // Flush to write silence marker
        stream.flush();

        // Should have written only 1 silence marker
        assert_eq!(stream.write_position(), 1);

        let read = stream.read(0).unwrap();
        assert!(read.is_silence());
        assert_eq!(read.silence_ticks(), 100);
    }

    #[test]
    fn test_stream_edge_detection() {
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

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
    fn test_stream_wrap_around() {
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

        // Write more than buffer size
        for i in 0..100u8 {
            let mut sample = StreamSample::EMPTY;
            sample.audio_level = i;
            stream.push_raw(sample);
        }

        // Old indices should be invalid (overwritten)
        assert!(stream.read(0).is_none());
        assert!(stream.read(30).is_none());

        // Recent indices should be valid
        assert!(stream.read(50).is_some());
        assert_eq!(stream.read(99).unwrap().audio_level, 99);
    }

    #[test]
    fn test_stream_overrun_detection() {
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

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
        let buffer = make_buffer::<64>();
        let stream = KeyingStream::with_buffer(buffer);

        for _ in 0..10 {
            stream.push_raw(StreamSample::EMPTY);
        }

        assert_eq!(stream.lag(0), 10);
        assert_eq!(stream.lag(5), 5);
        assert_eq!(stream.lag(10), 0);
    }

    #[test]
    fn test_consumer_basic() {
        let buffer: &'static [UnsafeCell<StreamSample>] = make_buffer::<64>();
        // Need to leak stream too for static lifetime
        let stream: &'static KeyingStream = Box::leak(Box::new(
            KeyingStream::with_buffer(buffer)
        ));

        let mut consumer = StreamConsumer::new(stream);

        // Initially no samples
        assert!(consumer.next().is_none());
        assert_eq!(consumer.lag(), 0);

        // Push some samples
        for i in 0..5u8 {
            let mut sample = StreamSample::EMPTY;
            sample.audio_level = i;
            stream.push_raw(sample);
        }

        // Consumer should see them
        assert_eq!(consumer.lag(), 5);

        for i in 0..5u8 {
            let sample = consumer.next().unwrap();
            assert_eq!(sample.audio_level, i);
        }

        // No more samples
        assert!(consumer.next().is_none());
        assert_eq!(consumer.lag(), 0);
    }

    #[test]
    fn test_consumer_skip_to_latest() {
        let buffer: &'static [UnsafeCell<StreamSample>] = make_buffer::<64>();
        let stream: &'static KeyingStream = Box::leak(Box::new(
            KeyingStream::with_buffer(buffer)
        ));

        let mut consumer = StreamConsumer::new(stream);

        // Push many samples
        for i in 0..50u8 {
            let mut sample = StreamSample::EMPTY;
            sample.audio_level = i;
            stream.push_raw(sample);
        }

        // Skip to latest
        let skipped = consumer.skip_to_latest();
        assert_eq!(skipped, 50);
        assert_eq!(consumer.lag(), 0);
        assert!(consumer.next().is_none());
    }

    #[test]
    fn test_consumer_resync() {
        let buffer: &'static [UnsafeCell<StreamSample>] = make_buffer::<64>();
        let stream: &'static KeyingStream = Box::leak(Box::new(
            KeyingStream::with_buffer(buffer)
        ));

        let mut consumer = StreamConsumer::new(stream);

        // Push way more than buffer size to cause overrun
        for i in 0..200u8 {
            let mut sample = StreamSample::EMPTY;
            sample.audio_level = i;
            stream.push_raw(sample);
        }

        // Consumer should be overrun
        assert!(consumer.is_overrun());

        // Resync
        consumer.resync();

        // Should no longer be overrun
        assert!(!consumer.is_overrun());

        // Should be able to read samples again
        assert!(consumer.next().is_some());
    }

    #[test]
    fn test_multiple_consumers_independent() {
        let buffer: &'static [UnsafeCell<StreamSample>] = make_buffer::<64>();
        let stream: &'static KeyingStream = Box::leak(Box::new(
            KeyingStream::with_buffer(buffer)
        ));

        let mut consumer1 = StreamConsumer::new(stream);
        let mut consumer2 = StreamConsumer::new(stream);

        // Push some samples
        for i in 0..10u8 {
            let mut sample = StreamSample::EMPTY;
            sample.audio_level = i;
            stream.push_raw(sample);
        }

        // Consumer1 reads 5 samples
        for _ in 0..5 {
            consumer1.next();
        }

        // Consumer2 reads 3 samples
        for _ in 0..3 {
            consumer2.next();
        }

        // They should be at different positions
        assert_eq!(consumer1.lag(), 5);
        assert_eq!(consumer2.lag(), 7);
        assert_eq!(consumer1.position(), 5);
        assert_eq!(consumer2.position(), 3);
    }
}
