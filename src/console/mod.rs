//! Serial console for configuration and diagnostics
//!
//! Lazy polling on Core 1 - no dedicated task.
//! Zero heap allocation - all static buffers.

pub mod error;
pub mod history;
pub mod parser;

pub use error::ConsoleError;
pub use history::History;
pub use parser::{parse_line, ParsedCommand};
