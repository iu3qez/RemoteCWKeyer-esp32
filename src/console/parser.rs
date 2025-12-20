//! Command line parser
//!
//! Simple split on whitespace, max 3 arguments.

/// Parsed command with up to 3 arguments
#[derive(Debug, Clone)]
pub struct ParsedCommand<'a> {
    /// The command name (first token)
    pub command: &'a str,
    /// Up to 3 arguments
    pub args: [Option<&'a str>; 3],
}

impl<'a> ParsedCommand<'a> {
    /// Create empty command
    pub const fn empty() -> Self {
        Self {
            command: "",
            args: [None, None, None],
        }
    }

    /// Get argument by index (0-based)
    pub fn arg(&self, idx: usize) -> Option<&'a str> {
        self.args.get(idx).copied().flatten()
    }
}

/// Parse a command line into command and arguments
pub fn parse_line(line: &str) -> ParsedCommand<'_> {
    let mut parts = line.split_whitespace();

    let command = parts.next().unwrap_or("");

    let mut args = [None, None, None];
    for (i, arg) in parts.take(3).enumerate() {
        args[i] = Some(arg);
    }

    ParsedCommand { command, args }
}
