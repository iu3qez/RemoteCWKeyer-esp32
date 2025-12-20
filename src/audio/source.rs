//! Audio source selector
//!
//! Atomic selection between silence, sidetone, and remote audio.

use core::sync::atomic::{AtomicU8, Ordering};

/// Audio source selection
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioSource {
    /// No audio output
    Silence = 0,
    /// Local sidetone (keyer)
    Sidetone = 1,
    /// Remote audio (received from network)
    Remote = 2,
}

impl AudioSource {
    /// Convert from u8
    #[inline]
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => Self::Sidetone,
            2 => Self::Remote,
            _ => Self::Silence,
        }
    }
}

impl From<u8> for AudioSource {
    fn from(v: u8) -> Self {
        Self::from_u8(v)
    }
}

impl From<AudioSource> for u8 {
    fn from(s: AudioSource) -> Self {
        s as u8
    }
}

/// Thread-safe audio source selector
pub struct AudioSourceSelector {
    source: AtomicU8,
}

impl AudioSourceSelector {
    /// Create new selector (starts at Silence)
    pub const fn new() -> Self {
        Self {
            source: AtomicU8::new(AudioSource::Silence as u8),
        }
    }

    /// Get current audio source
    #[inline]
    pub fn get(&self) -> AudioSource {
        AudioSource::from_u8(self.source.load(Ordering::Acquire))
    }

    /// Set audio source
    #[inline]
    pub fn set(&self, source: AudioSource) {
        self.source.store(source as u8, Ordering::Release);
    }

    /// Atomically swap to new source, return old
    #[inline]
    pub fn swap(&self, source: AudioSource) -> AudioSource {
        AudioSource::from_u8(self.source.swap(source as u8, Ordering::AcqRel))
    }
}

impl Default for AudioSourceSelector {
    fn default() -> Self {
        Self::new()
    }
}

// Allow sharing between threads
unsafe impl Sync for AudioSourceSelector {}
