//! Main console struct integrating all components

use core::fmt::Write;
use super::{
    LineBuffer, History, Completer,
    parse_line, execute, command_names, ConsoleError,
};

/// Version string (set by build.rs, includes git hash)
pub const VERSION: &str = env!("VERSION_STRING");

/// Console state machine
pub struct Console {
    line: LineBuffer,
    history: History,
    completer: Completer,
    /// Escape sequence state
    escape_state: EscapeState,
}

#[derive(Clone, Copy, PartialEq)]
enum EscapeState {
    Normal,
    Escape,      // Got ESC
    Bracket,     // Got ESC [
}

impl Console {
    /// Create new console
    pub const fn new() -> Self {
        Self {
            line: LineBuffer::new(),
            history: History::new(),
            completer: Completer::new(),
            escape_state: EscapeState::Normal,
        }
    }

    /// Process a single input byte
    ///
    /// Returns Some(result) if command completed, None if more input needed.
    pub fn process_byte(&mut self, byte: u8, out: &mut dyn Write) -> Option<Result<(), ConsoleError>> {
        match self.escape_state {
            EscapeState::Normal => self.process_normal(byte, out),
            EscapeState::Escape => {
                if byte == b'[' {
                    self.escape_state = EscapeState::Bracket;
                } else {
                    self.escape_state = EscapeState::Normal;
                }
                None
            }
            EscapeState::Bracket => {
                self.escape_state = EscapeState::Normal;
                match byte {
                    b'A' => self.handle_up(out),    // Up arrow
                    b'B' => self.handle_down(out),  // Down arrow
                    _ => {}
                }
                None
            }
        }
    }

    fn process_normal(&mut self, byte: u8, out: &mut dyn Write) -> Option<Result<(), ConsoleError>> {
        match byte {
            // Enter
            b'\r' | b'\n' => {
                let _ = writeln!(out);
                let line = self.line.as_str();

                if !line.is_empty() {
                    self.history.push(line);
                    let cmd = parse_line(line);
                    let result = execute(&cmd, out);
                    self.line.clear();
                    self.print_prompt(out);
                    return Some(result);
                }

                self.print_prompt(out);
                None
            }

            // Backspace
            0x7F | 0x08 => {
                if !self.line.is_empty() {
                    self.line.backspace();
                    // Echo: backspace, space, backspace
                    let _ = write!(out, "\x08 \x08");
                }
                self.completer.reset();
                self.history.reset_nav();
                None
            }

            // Tab
            b'\t' => {
                self.handle_tab(out);
                None
            }

            // Escape
            0x1B => {
                self.escape_state = EscapeState::Escape;
                None
            }

            // Ctrl+C
            0x03 => {
                let _ = writeln!(out, "^C");
                self.line.clear();
                self.print_prompt(out);
                None
            }

            // Ctrl+U (clear line)
            0x15 => {
                // Clear the displayed line
                for _ in 0..self.line.len() {
                    let _ = write!(out, "\x08 \x08");
                }
                self.line.clear();
                None
            }

            // Printable character
            0x20..=0x7E => {
                self.line.push(byte);
                let _ = write!(out, "{}", byte as char);
                self.completer.reset();
                self.history.reset_nav();
                None
            }

            _ => None,
        }
    }

    fn handle_tab(&mut self, out: &mut dyn Write) {
        let input = self.line.as_str();

        // Count words to determine what to complete
        let word_count = input.split_whitespace().count();
        let last_word_start = input.rfind(' ').map(|i| i + 1).unwrap_or(0);
        let prefix = &input[last_word_start..];

        let completion = if word_count <= 1 && !input.ends_with(' ') {
            // Complete command (first word, no trailing space)
            self.completer.complete(prefix, command_names())
        } else {
            // Complete parameter (after command)
            #[cfg(not(test))]
            {
                use crate::config::param_names;
                self.completer.complete(prefix, param_names())
            }
            #[cfg(test)]
            {
                None
            }
        };

        if let Some(completed) = completion {
            // Clear current word and replace with completion
            let prefix_len = prefix.len();
            for _ in 0..prefix_len {
                self.line.backspace();
                let _ = write!(out, "\x08 \x08");
            }

            for c in completed.bytes() {
                self.line.push(c);
                let _ = write!(out, "{}", c as char);
            }
        }
    }

    fn handle_up(&mut self, out: &mut dyn Write) {
        if let Some(prev) = self.history.get_prev() {
            self.replace_line(prev, out);
        }
    }

    fn handle_down(&mut self, out: &mut dyn Write) {
        if let Some(next) = self.history.get_next() {
            self.replace_line(next, out);
        } else {
            // Clear to empty line
            self.replace_line("", out);
        }
    }

    fn replace_line(&mut self, new_line: &str, out: &mut dyn Write) {
        // Clear displayed line
        for _ in 0..self.line.len() {
            let _ = write!(out, "\x08 \x08");
        }

        // Set and display new line
        self.line.set(new_line);
        let _ = write!(out, "{}", new_line);
    }

    /// Print the prompt
    pub fn print_prompt(&self, out: &mut dyn Write) {
        let _ = write!(out, "{}> ", VERSION);
    }

    /// Print welcome banner
    pub fn print_banner(&self, out: &mut dyn Write) {
        let _ = writeln!(out, "\r\n{}", VERSION);
        let _ = writeln!(out, "Type 'help' for commands.\r");
        self.print_prompt(out);
    }
}
