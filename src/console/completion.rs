//! Tab completion with cycling

/// Tab completion state
pub struct Completer {
    /// Prefix being completed (stored for cycle detection)
    prefix: [u8; 32],
    prefix_len: usize,
    /// Current match index for cycling
    match_idx: usize,
    /// Whether we're actively cycling
    cycling: bool,
}

impl Completer {
    /// Create new completer
    pub const fn new() -> Self {
        Self {
            prefix: [0u8; 32],
            prefix_len: 0,
            match_idx: 0,
            cycling: false,
        }
    }

    /// Complete prefix, cycling through matches on repeated calls
    ///
    /// Returns the completed string, or None if no match.
    pub fn complete<'a, I>(&mut self, prefix: &str, candidates: I) -> Option<&'a str>
    where
        I: Iterator<Item = &'a str> + Clone,
    {
        let prefix_bytes = prefix.as_bytes();

        // Check if prefix changed (reset cycling)
        let same_prefix = prefix_bytes.len() == self.prefix_len
            && prefix_bytes == &self.prefix[..self.prefix_len];

        if !same_prefix {
            // New prefix, start fresh
            self.prefix_len = prefix_bytes.len().min(32);
            self.prefix[..self.prefix_len].copy_from_slice(&prefix_bytes[..self.prefix_len]);
            self.match_idx = 0;
            self.cycling = false;
        } else if self.cycling {
            // Same prefix, advance to next match
            self.match_idx += 1;
        }

        // Collect matching candidates into static buffer
        let mut matches: [Option<&str>; 32] = [None; 32];
        let mut match_count = 0;

        for c in candidates {
            if c.starts_with(prefix) && match_count < 32 {
                matches[match_count] = Some(c);
                match_count += 1;
            }
        }

        if match_count == 0 {
            self.cycling = false;
            return None;
        }

        // Wrap around
        if self.match_idx >= match_count {
            self.match_idx = 0;
        }

        self.cycling = true;
        matches[self.match_idx]
    }

    /// Reset completion state (call when user types non-tab)
    pub fn reset(&mut self) {
        self.cycling = false;
        self.match_idx = 0;
    }
}
