//! Serial console for configuration and diagnostics
//!
//! Lazy polling on Core 1 - no dedicated task.
//! Zero heap allocation - all static buffers.

pub mod commands;
pub mod completion;
pub mod error;
pub mod history;
pub mod line_buffer;
pub mod parser;

pub use commands::{execute, COMMANDS, command_names};
pub use completion::Completer;
pub use error::ConsoleError;
pub use history::History;
pub use line_buffer::LineBuffer;
pub use parser::{parse_line, ParsedCommand};
