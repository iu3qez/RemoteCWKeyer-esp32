//! Lock-free audio ring buffer
//!
//! SPSC (single producer, single consumer) for audio samples.
//! Uses atomic indices for thread-safe access.

use core::sync::atomic::{AtomicUsize, Ordering};

/// Audio ring buffer with static size
///
/// N must be a power of 2 for efficient modulo.
pub struct AudioRingBuffer<const N: usize> {
    buffer: [i16; N],
    write_idx: AtomicUsize,
    read_idx: AtomicUsize,
}

impl<const N: usize> AudioRingBuffer<N> {
    /// Create new empty buffer
    pub const fn new() -> Self {
        // Compile-time check that N is power of 2
        const { assert!(N.is_power_of_two(), "Buffer size must be power of 2") };

        Self {
            buffer: [0i16; N],
            write_idx: AtomicUsize::new(0),
            read_idx: AtomicUsize::new(0),
        }
    }

    /// Push a sample to the buffer
    ///
    /// If buffer is full, overwrites oldest sample.
    #[inline]
    pub fn push(&mut self, sample: i16) {
        let write = self.write_idx.load(Ordering::Relaxed);
        let read = self.read_idx.load(Ordering::Relaxed);

        // Check if we need to advance read (overflow)
        if write.wrapping_sub(read) >= N {
            self.read_idx.store(read.wrapping_add(1), Ordering::Relaxed);
        }

        self.buffer[write & (N - 1)] = sample;
        self.write_idx.store(write.wrapping_add(1), Ordering::Release);
    }

    /// Pop a sample from the buffer
    ///
    /// Returns None if buffer is empty.
    #[inline]
    pub fn pop(&mut self) -> Option<i16> {
        let write = self.write_idx.load(Ordering::Acquire);
        let read = self.read_idx.load(Ordering::Relaxed);

        if write == read {
            return None; // Empty
        }

        let sample = self.buffer[read & (N - 1)];
        self.read_idx.store(read.wrapping_add(1), Ordering::Relaxed);
        Some(sample)
    }

    /// Read multiple samples into a slice
    ///
    /// Fills as many samples as available, zeros the rest.
    /// Returns number of samples actually read.
    #[inline]
    pub fn read_into(&mut self, output: &mut [i16]) -> usize {
        let mut count = 0;

        for sample in output.iter_mut() {
            match self.pop() {
                Some(s) => {
                    *sample = s;
                    count += 1;
                }
                None => {
                    *sample = 0; // Underrun: fill with silence
                }
            }
        }

        count
    }

    /// Get number of samples in buffer
    #[inline]
    pub fn len(&self) -> usize {
        let write = self.write_idx.load(Ordering::Acquire);
        let read = self.read_idx.load(Ordering::Relaxed);
        write.wrapping_sub(read).min(N)
    }

    /// Check if buffer is empty
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Get available space in buffer
    #[inline]
    pub fn available(&self) -> usize {
        N - self.len()
    }

    /// Clear the buffer
    #[inline]
    pub fn clear(&mut self) {
        let write = self.write_idx.load(Ordering::Relaxed);
        self.read_idx.store(write, Ordering::Relaxed);
    }
}

impl<const N: usize> Default for AudioRingBuffer<N> {
    fn default() -> Self {
        Self::new()
    }
}
