//! Line buffer for console input

use super::history::LINE_SIZE;

/// Line input buffer
pub struct LineBuffer {
    buf: [u8; LINE_SIZE],
    len: usize,
}

impl LineBuffer {
    /// Create empty buffer
    pub const fn new() -> Self {
        Self {
            buf: [0u8; LINE_SIZE],
            len: 0,
        }
    }

    /// Push a character
    pub fn push(&mut self, c: u8) {
        if self.len < LINE_SIZE {
            self.buf[self.len] = c;
            self.len += 1;
        }
    }

    /// Remove last character
    pub fn backspace(&mut self) {
        if self.len > 0 {
            self.len -= 1;
        }
    }

    /// Clear buffer
    pub fn clear(&mut self) {
        self.len = 0;
    }

    /// Set buffer contents from string
    pub fn set(&mut self, s: &str) {
        let bytes = s.as_bytes();
        let copy_len = bytes.len().min(LINE_SIZE);
        self.buf[..copy_len].copy_from_slice(&bytes[..copy_len]);
        self.len = copy_len;
    }

    /// Get buffer as string slice
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(&self.buf[..self.len]).unwrap_or("")
    }

    /// Get buffer length
    pub fn len(&self) -> usize {
        self.len
    }

    /// Check if empty
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Get raw bytes
    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
}
