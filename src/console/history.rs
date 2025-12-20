//! Command history with ring buffer
//!
//! Static allocation, 4 entries of 64 bytes each.

/// Maximum line length
pub const LINE_SIZE: usize = 64;

/// Number of history entries
pub const HISTORY_SIZE: usize = 4;

/// Command history ring buffer
pub struct History {
    /// Ring buffer of command lines
    entries: [[u8; LINE_SIZE]; HISTORY_SIZE],
    /// Length of each entry
    lengths: [usize; HISTORY_SIZE],
    /// Write index (next slot to write)
    write_idx: usize,
    /// Number of valid entries
    count: usize,
    /// Current navigation position (0 = newest, count-1 = oldest)
    nav_pos: Option<usize>,
}

impl History {
    /// Create empty history
    pub const fn new() -> Self {
        Self {
            entries: [[0u8; LINE_SIZE]; HISTORY_SIZE],
            lengths: [0; HISTORY_SIZE],
            write_idx: 0,
            count: 0,
            nav_pos: None,
        }
    }

    /// Push a new command into history
    pub fn push(&mut self, line: &str) {
        let bytes = line.as_bytes();
        let len = bytes.len().min(LINE_SIZE);

        self.entries[self.write_idx][..len].copy_from_slice(&bytes[..len]);
        self.lengths[self.write_idx] = len;

        self.write_idx = (self.write_idx + 1) % HISTORY_SIZE;
        self.count = (self.count + 1).min(HISTORY_SIZE);
        self.nav_pos = None; // Reset navigation
    }

    /// Get previous (older) command
    pub fn get_prev(&mut self) -> Option<&str> {
        if self.count == 0 {
            return None;
        }

        let pos = match self.nav_pos {
            None => 0, // Start at newest
            Some(p) if p + 1 < self.count => p + 1, // Go older
            Some(p) => p, // Already at oldest
        };

        self.nav_pos = Some(pos);
        self.get_at(pos)
    }

    /// Get next (newer) command
    pub fn get_next(&mut self) -> Option<&str> {
        match self.nav_pos {
            None => None,
            Some(0) => {
                self.nav_pos = None;
                None // Back to current input
            }
            Some(p) => {
                self.nav_pos = Some(p - 1);
                self.get_at(p - 1)
            }
        }
    }

    /// Reset navigation (call when user types)
    pub fn reset_nav(&mut self) {
        self.nav_pos = None;
    }

    /// Get entry at navigation position (0 = newest)
    fn get_at(&self, nav_pos: usize) -> Option<&str> {
        if nav_pos >= self.count {
            return None;
        }

        // Calculate actual index in ring buffer
        // write_idx points to next write slot, so newest is at write_idx - 1
        let idx = (self.write_idx + HISTORY_SIZE - 1 - nav_pos) % HISTORY_SIZE;
        let len = self.lengths[idx];

        core::str::from_utf8(&self.entries[idx][..len]).ok()
    }
}
