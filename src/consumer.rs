//! Stream consumers for RustRemoteCWKeyer.
//!
//! Two types of consumers:
//! - **HardRtConsumer**: Must keep up or FAULT (audio, TX)
//! - **BestEffortConsumer**: Skips if behind (decoder, remote)
//!
//! # Rules (from ARCHITECTURE.md §4, §5)
//!
//! - RULE 4.1.1: Hard RT consumer has maximum allowed lag
//! - RULE 4.1.2: Exceeding maximum lag triggers FAULT
//! - RULE 5.1.1: Best-Effort consumer never FAULTs
//! - RULE 5.1.2: If behind, Best-Effort skips forward

use crate::fault::{FaultCode, FaultState};
use crate::sample::StreamSample;
use crate::stream::KeyingStream;

/// Hard Real-Time consumer.
///
/// Used for audio output and TX keying where timing is critical.
///
/// # Contract
///
/// "I MUST keep up with the stream, or I FAULT."
///
/// If the consumer falls behind by more than `max_lag` samples,
/// it triggers a FAULT and stops processing. This is correct behavior:
/// corrupted timing is worse than silence.
///
/// # Example
///
/// ```ignore
/// static STREAM: KeyingStream = KeyingStream::new();
/// static FAULT: FaultState = FaultState::new();
///
/// let mut consumer = HardRtConsumer::new(&STREAM, &FAULT, 2);
///
/// loop {
///     match consumer.tick() {
///         Ok(Some(sample)) => {
///             audio_set(sample.local_key);
///             tx_set(sample.local_key);
///         }
///         Ok(None) => {} // No new data
///         Err(fault) => {
///             audio_off();
///             tx_off();
///             // Handle fault...
///         }
///     }
/// }
/// ```
pub struct HardRtConsumer<'a, const N: usize = { crate::stream::DEFAULT_STREAM_SIZE }> {
    stream: &'a KeyingStream<N>,
    fault: &'a FaultState,
    read_idx: u32,
    max_lag: u32,
}

impl<'a, const N: usize> HardRtConsumer<'a, N> {
    /// Create a new Hard RT consumer.
    ///
    /// # Arguments
    ///
    /// * `stream` - The keying stream to consume
    /// * `fault` - Fault state to set on timing failure
    /// * `max_lag` - Maximum allowed samples behind before FAULT
    pub fn new(stream: &'a KeyingStream<N>, fault: &'a FaultState, max_lag: u32) -> Self {
        Self {
            stream,
            fault,
            read_idx: stream.write_head(),
            max_lag,
        }
    }

    /// Tick the consumer: read next sample if available.
    ///
    /// # Returns
    ///
    /// - `Ok(Some(sample))` - New sample available
    /// - `Ok(None)` - No new data (caught up with producer)
    /// - `Err(FaultCode)` - Timing failure, FAULT triggered
    ///
    /// # Timing
    ///
    /// This operation completes in O(1) time, typically < 100ns.
    /// Never blocks, never allocates.
    #[inline]
    pub fn tick(&mut self) -> Result<Option<StreamSample>, FaultCode> {
        // Check overrun FIRST (most severe)
        if self.stream.is_overrun(self.read_idx) {
            let lag = self.stream.lag(self.read_idx);
            self.fault.set(FaultCode::Overrun, lag);
            return Err(FaultCode::Overrun);
        }

        // Check latency
        let lag = self.stream.lag(self.read_idx);
        if lag > self.max_lag {
            self.fault.set(FaultCode::LatencyExceeded, lag);
            return Err(FaultCode::LatencyExceeded);
        }

        // Try to read
        match self.stream.read(self.read_idx) {
            Some(sample) => {
                self.read_idx = self.read_idx.wrapping_add(1);
                Ok(Some(sample))
            }
            None => Ok(None),
        }
    }

    /// Resync to current write head (after fault recovery).
    ///
    /// Call this after clearing the fault to resume operation.
    #[inline]
    pub fn resync(&mut self) {
        self.read_idx = self.stream.write_head();
    }

    /// Get current lag (samples behind producer).
    #[inline]
    pub fn lag(&self) -> u32 {
        self.stream.lag(self.read_idx)
    }

    /// Get current read index.
    #[inline]
    pub fn read_idx(&self) -> u32 {
        self.read_idx
    }
}

/// Best-Effort consumer.
///
/// Used for decoder, remote forwarding, diagnostics where timing
/// is not critical and falling behind is acceptable.
///
/// # Contract
///
/// "I process when I can. I skip if I fall behind."
///
/// Never FAULTs. If too far behind, skips forward to catch up.
/// Tracks dropped samples for diagnostics.
///
/// # Example
///
/// ```ignore
/// static STREAM: KeyingStream = KeyingStream::new();
///
/// let mut consumer = BestEffortConsumer::new(&STREAM);
///
/// loop {
///     while let Some(sample) = consumer.tick() {
///         decoder.process(sample);
///     }
///     delay_ms(5);
/// }
/// ```
pub struct BestEffortConsumer<'a, const N: usize = { crate::stream::DEFAULT_STREAM_SIZE }> {
    stream: &'a KeyingStream<N>,
    read_idx: u32,
    dropped: u32,
}

impl<'a, const N: usize> BestEffortConsumer<'a, N> {
    /// Create a new Best-Effort consumer.
    pub fn new(stream: &'a KeyingStream<N>) -> Self {
        Self {
            stream,
            read_idx: stream.write_head(),
            dropped: 0,
        }
    }

    /// Tick the consumer: read next sample if available.
    ///
    /// If consumer has fallen too far behind, skips forward
    /// (drops samples) and continues. Never returns an error.
    ///
    /// # Returns
    ///
    /// - `Some(sample)` - Next sample (may have skipped some)
    /// - `None` - No new data (caught up with producer)
    #[inline]
    pub fn tick(&mut self) -> Option<StreamSample> {
        // Check if we're too far behind (overrun)
        if self.stream.is_overrun(self.read_idx) {
            let write = self.stream.write_head();
            let skipped = write.wrapping_sub(self.read_idx);

            // Skip to half-buffer behind (leave room for catchup)
            self.read_idx = write.wrapping_sub((N / 2) as u32);
            self.dropped = self.dropped.saturating_add(skipped);
        }

        // Try to read
        match self.stream.read(self.read_idx) {
            Some(sample) => {
                self.read_idx = self.read_idx.wrapping_add(1);
                Some(sample)
            }
            None => None,
        }
    }

    /// Drain all available samples.
    ///
    /// Returns an iterator that yields all samples from current
    /// read position to write head. Useful for batch processing.
    #[inline]
    pub fn drain(&mut self) -> DrainIterator<'_, 'a, N> {
        DrainIterator { consumer: self }
    }

    /// Get count of dropped samples (due to falling behind).
    #[inline]
    pub fn dropped(&self) -> u32 {
        self.dropped
    }

    /// Get current lag (samples behind producer).
    #[inline]
    pub fn lag(&self) -> u32 {
        self.stream.lag(self.read_idx)
    }

    /// Get current read index.
    #[inline]
    pub fn read_idx(&self) -> u32 {
        self.read_idx
    }

    /// Reset dropped counter (e.g., after reporting).
    #[inline]
    pub fn reset_dropped(&mut self) {
        self.dropped = 0;
    }
}

/// Iterator for draining all available samples.
pub struct DrainIterator<'c, 'a, const N: usize> {
    consumer: &'c mut BestEffortConsumer<'a, N>,
}

impl<'c, 'a, const N: usize> Iterator for DrainIterator<'c, 'a, N> {
    type Item = StreamSample;

    fn next(&mut self) -> Option<Self::Item> {
        self.consumer.tick()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sample::GpioState;

    #[test]
    fn test_hard_rt_consumer_basic() {
        let stream = KeyingStream::<64>::new();
        let fault = FaultState::new();
        let mut consumer = HardRtConsumer::new(&stream, &fault, 10);

        // Push a sample
        let mut sample = StreamSample::EMPTY;
        sample.local_key = true;
        stream.push_raw(sample);

        // Should read it
        let result = consumer.tick();
        assert!(result.is_ok());
        assert!(result.unwrap().is_some());
    }

    #[test]
    fn test_hard_rt_consumer_fault_on_lag() {
        let stream = KeyingStream::<64>::new();
        let fault = FaultState::new();
        let mut consumer = HardRtConsumer::new(&stream, &fault, 5);

        // Push more than max_lag samples
        for _ in 0..10 {
            stream.push_raw(StreamSample::EMPTY);
        }

        // Should fault
        let result = consumer.tick();
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), FaultCode::LatencyExceeded);
        assert!(fault.is_active());
    }

    #[test]
    fn test_hard_rt_consumer_resync() {
        let stream = KeyingStream::<64>::new();
        let fault = FaultState::new();
        let mut consumer = HardRtConsumer::new(&stream, &fault, 5);

        // Push many samples to cause fault
        for _ in 0..100 {
            stream.push_raw(StreamSample::EMPTY);
        }

        // Will fault
        let _ = consumer.tick();
        assert!(fault.is_active());

        // Resync and clear
        consumer.resync();
        fault.clear();

        // Should work again
        stream.push_raw(StreamSample::EMPTY);
        let result = consumer.tick();
        assert!(result.is_ok());
    }

    #[test]
    fn test_best_effort_consumer_skips() {
        let stream = KeyingStream::<64>::new();
        let mut consumer = BestEffortConsumer::new(&stream);

        // Push many samples to cause overrun
        for i in 0..100u8 {
            let mut sample = StreamSample::EMPTY;
            sample.gpio = GpioState { bits: i };
            stream.push_raw(sample);
        }

        // Should skip forward, not fail
        let sample = consumer.tick();
        assert!(sample.is_some());

        // Should have recorded drops
        assert!(consumer.dropped() > 0);
    }

    #[test]
    fn test_best_effort_drain() {
        let stream = KeyingStream::<64>::new();
        let mut consumer = BestEffortConsumer::new(&stream);

        // Push 5 samples
        for _ in 0..5 {
            stream.push_raw(StreamSample::EMPTY);
        }

        // Drain should get all 5
        let count = consumer.drain().count();
        assert_eq!(count, 5);

        // Drain again should get 0
        let count = consumer.drain().count();
        assert_eq!(count, 0);
    }
}
