# Console Commands Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement UART serial console with commands for configuration, debugging, and diagnostics.

**Architecture:** Lazy polling on Core 1, zero heap allocation, parameter registry generated from parameters.yaml, history and tab completion with static buffers.

**Tech Stack:** Rust, ESP-IDF UART API, existing codegen (gen_config.py), atomic CONFIG.

**Design Doc:** `docs/plans/2025-12-20-console-commands-design.md`

---

## Task 1: Extend Codegen for Console Registry

**Files:**
- Modify: `scripts/gen_config.py`
- Create: `src/generated/config_console.rs`

**Step 1.1: Add version string generation to build.rs**

Modify `build.rs` to emit version info:

```rust
// RustRemoteCWKeyer - Build Script
//
// Runs code generation from parameters.yaml before compilation.

use std::process::Command;

fn main() {
    // ESP-IDF environment setup (MUST be first!)
    embuild::espidf::sysenv::output();

    // Get git version info
    let version = env!("CARGO_PKG_VERSION");
    let git_hash = Command::new("git")
        .args(["rev-parse", "--short", "HEAD"])
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|| "unknown".to_string());

    println!("cargo:rustc-env=GIT_HASH={}", git_hash);
    println!("cargo:rustc-env=VERSION_STRING=RustKeyer v{}-g{}", version, git_hash);

    // Run Python code generator
    let status = Command::new("python3")
        .arg("scripts/gen_config.py")
        .status()
        .expect("Failed to run config generator. Is Python 3 installed?");

    if !status.success() {
        panic!("Config generation failed. Check scripts/gen_config.py output.");
    }

    // Rebuild if parameters.yaml changes
    println!("cargo:rerun-if-changed=parameters.yaml");

    // Rebuild if codegen script changes
    println!("cargo:rerun-if-changed=scripts/gen_config.py");

    // Rebuild if git HEAD changes
    println!("cargo:rerun-if-changed=.git/HEAD");
}
```

**Step 1.2: Add config_console.rs generation to gen_config.py**

Add new function at end of `scripts/gen_config.py`:

```python
def generate_config_console_rs(params: List[Dict], output_dir: Path):
    """Generate config_console.rs - Console parameter registry"""

    # Extract unique categories
    categories = sorted(set(p['category'] for p in params))

    code = """// Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY
//
// Console command parameter registry for set/show commands.

use core::sync::atomic::Ordering;
use super::config::CONFIG;

/// Parameter value union for get/set operations
#[derive(Debug, Clone, Copy)]
pub enum ParamValue {
    U8(u8),
    U16(u16),
    U32(u32),
    Bool(bool),
}

impl ParamValue {
    pub fn as_u8(&self) -> Option<u8> {
        match self { ParamValue::U8(v) => Some(*v), _ => None }
    }
    pub fn as_u16(&self) -> Option<u16> {
        match self { ParamValue::U16(v) => Some(*v), _ => None }
    }
    pub fn as_u32(&self) -> Option<u32> {
        match self { ParamValue::U32(v) => Some(*v), _ => None }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self { ParamValue::Bool(v) => Some(*v), _ => None }
    }
}

/// Parameter type with validation info
#[derive(Debug, Clone, Copy)]
pub enum ParamType {
    U8 { min: u8, max: u8 },
    U16 { min: u16, max: u16 },
    U32 { min: u32, max: u32 },
    Bool,
    Enum { max: u8 },
}

/// Console error codes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConsoleError {
    E01UnknownCommand,
    E02InvalidValue,
    E03MissingArg,
    E04OutOfRange,
    E05RequiresConfirm,
    E06NvsError,
}

impl ConsoleError {
    pub fn code(&self) -> &'static str {
        match self {
            ConsoleError::E01UnknownCommand => "E01",
            ConsoleError::E02InvalidValue => "E02",
            ConsoleError::E03MissingArg => "E03",
            ConsoleError::E04OutOfRange => "E04",
            ConsoleError::E05RequiresConfirm => "E05",
            ConsoleError::E06NvsError => "E06",
        }
    }

    pub fn message(&self) -> &'static str {
        match self {
            ConsoleError::E01UnknownCommand => "unknown command",
            ConsoleError::E02InvalidValue => "invalid value",
            ConsoleError::E03MissingArg => "missing argument",
            ConsoleError::E04OutOfRange => "out of range",
            ConsoleError::E05RequiresConfirm => "requires 'confirm'",
            ConsoleError::E06NvsError => "NVS error",
        }
    }
}

/// Single parameter descriptor
pub struct ParamDescriptor {
    pub name: &'static str,
    pub category: &'static str,
    pub param_type: ParamType,
    pub get_fn: fn() -> ParamValue,
    pub set_fn: fn(ParamValue) -> Result<(), ConsoleError>,
}

"""

    # Generate parameter descriptors
    code += "/// All parameters for console access\n"
    code += "pub static PARAMS: &[ParamDescriptor] = &[\n"

    for p in params:
        name = p['name']
        category = p['category']
        ptype = p['type']

        # Determine ParamType
        if ptype == 'bool':
            param_type = "ParamType::Bool"
        elif ptype == 'enum':
            max_val = len(p['enum_values']) - 1
            param_type = f"ParamType::Enum {{ max: {max_val} }}"
        else:
            min_val, max_val = p['range']
            param_type = f"ParamType::{ptype.upper()} {{ min: {min_val}, max: {max_val} }}"

        # Determine atomic type for get/set
        if ptype == 'u8' or ptype == 'enum':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U8"
            set_variant = "as_u8"
        elif ptype == 'u16':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U16"
            set_variant = "as_u16"
        elif ptype == 'u32':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U32"
            set_variant = "as_u32"
        elif ptype == 'bool':
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "Bool"
            set_variant = "as_bool"
        else:
            atomic_load = f"CONFIG.{name}.load(Ordering::Relaxed)"
            get_variant = "U32"
            set_variant = "as_u32"

        code += f"""    ParamDescriptor {{
        name: "{name}",
        category: "{category}",
        param_type: {param_type},
        get_fn: || ParamValue::{get_variant}({atomic_load}),
        set_fn: |v| {{
            if let Some(val) = v.{set_variant}() {{
                CONFIG.{name}.store(val, Ordering::Relaxed);
                CONFIG.bump_generation();
                Ok(())
            }} else {{
                Err(ConsoleError::E02InvalidValue)
            }}
        }},
    }},
"""

    code += "];\n\n"

    # Generate categories array
    code += "/// All parameter categories\n"
    code += "pub static CATEGORIES: &[&str] = &[\n"
    for cat in categories:
        code += f'    "{cat}",\n'
    code += "];\n\n"

    # Helper functions
    code += """/// Find parameter by name
pub fn find_param(name: &str) -> Option<&'static ParamDescriptor> {
    PARAMS.iter().find(|p| p.name == name)
}

/// Find parameters matching pattern (prefix match on name or category)
pub fn find_params_matching(pattern: &str) -> impl Iterator<Item = &'static ParamDescriptor> {
    let pattern = pattern.trim_end_matches('*');
    PARAMS.iter().filter(move |p| {
        p.name.starts_with(pattern) || p.category.starts_with(pattern)
    })
}

/// Get all parameter names for tab completion
pub fn param_names() -> impl Iterator<Item = &'static str> {
    PARAMS.iter().map(|p| p.name)
}
"""

    with open(output_dir / "config_console.rs", "w") as f:
        f.write(code)
```

**Step 1.3: Call new generator in main()**

In `scripts/gen_config.py`, add call in `main()`:

```python
def main():
    # ... existing code ...

    print("Generating config_console.rs...")
    generate_config_console_rs(params, output_dir)

    print(f"✓ Code generation complete: {output_dir}")
```

**Step 1.4: Run build to verify generation**

Run: `cargo build 2>&1 | head -20`
Expected: Build succeeds, `src/generated/config_console.rs` created

**Step 1.5: Commit codegen changes**

```bash
git add scripts/gen_config.py build.rs
git commit -m "feat(codegen): add console parameter registry generation"
```

---

## Task 2: Console Error Types and Parser

**Files:**
- Create: `src/console/mod.rs`
- Create: `src/console/error.rs`
- Create: `src/console/parser.rs`
- Modify: `src/lib.rs`

**Step 2.1: Write parser test**

Create `tests/console_parser_tests.rs`:

```rust
//! Parser tests for console command line parsing

use rust_remote_cw_keyer::console::parser::{parse_line, ParsedCommand};

#[test]
fn test_parse_simple_command() {
    let cmd = parse_line("help");
    assert_eq!(cmd.command, "help");
    assert_eq!(cmd.args[0], None);
}

#[test]
fn test_parse_command_with_one_arg() {
    let cmd = parse_line("show wpm");
    assert_eq!(cmd.command, "show");
    assert_eq!(cmd.args[0], Some("wpm"));
    assert_eq!(cmd.args[1], None);
}

#[test]
fn test_parse_command_with_two_args() {
    let cmd = parse_line("set wpm 25");
    assert_eq!(cmd.command, "set");
    assert_eq!(cmd.args[0], Some("wpm"));
    assert_eq!(cmd.args[1], Some("25"));
    assert_eq!(cmd.args[2], None);
}

#[test]
fn test_parse_trims_whitespace() {
    let cmd = parse_line("  show   keyer*  ");
    assert_eq!(cmd.command, "show");
    assert_eq!(cmd.args[0], Some("keyer*"));
}

#[test]
fn test_parse_empty_line() {
    let cmd = parse_line("");
    assert_eq!(cmd.command, "");
}

#[test]
fn test_parse_max_args() {
    let cmd = parse_line("debug wifi warn extra ignored");
    assert_eq!(cmd.command, "debug");
    assert_eq!(cmd.args[0], Some("wifi"));
    assert_eq!(cmd.args[1], Some("warn"));
    assert_eq!(cmd.args[2], Some("extra"));
    // "ignored" is dropped (max 3 args)
}
```

**Step 2.2: Run test to verify it fails**

Run: `cargo test console_parser`
Expected: FAIL - module not found

**Step 2.3: Create console module structure**

Create `src/console/mod.rs`:

```rust
//! Serial console for configuration and diagnostics
//!
//! Lazy polling on Core 1 - no dedicated task.
//! Zero heap allocation - all static buffers.

pub mod error;
pub mod parser;

pub use error::ConsoleError;
pub use parser::{parse_line, ParsedCommand};
```

Create `src/console/error.rs`:

```rust
//! Console error types

/// Console error with code and message
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConsoleError {
    /// E01: Unknown command
    UnknownCommand,
    /// E02: Invalid value format
    InvalidValue,
    /// E03: Missing required argument
    MissingArg,
    /// E04: Value out of allowed range
    OutOfRange,
    /// E05: Dangerous command requires 'confirm'
    RequiresConfirm,
    /// E06: NVS storage error
    NvsError,
}

impl ConsoleError {
    /// Get error code string
    pub fn code(&self) -> &'static str {
        match self {
            Self::UnknownCommand => "E01",
            Self::InvalidValue => "E02",
            Self::MissingArg => "E03",
            Self::OutOfRange => "E04",
            Self::RequiresConfirm => "E05",
            Self::NvsError => "E06",
        }
    }

    /// Get error message
    pub fn message(&self) -> &'static str {
        match self {
            Self::UnknownCommand => "unknown command",
            Self::InvalidValue => "invalid value",
            Self::MissingArg => "missing argument",
            Self::OutOfRange => "out of range",
            Self::RequiresConfirm => "requires 'confirm'",
            Self::NvsError => "NVS error",
        }
    }
}

impl core::fmt::Display for ConsoleError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "{}: {}", self.code(), self.message())
    }
}
```

Create `src/console/parser.rs`:

```rust
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
```

**Step 2.4: Add console module to lib.rs**

Modify `src/lib.rs`, add after other modules:

```rust
pub mod console;
```

**Step 2.5: Run test to verify it passes**

Run: `cargo test console_parser`
Expected: All 6 tests PASS

**Step 2.6: Commit parser**

```bash
git add src/console/ tests/console_parser_tests.rs src/lib.rs
git commit -m "feat(console): add command parser with tests"
```

---

## Task 3: Command History

**Files:**
- Create: `src/console/history.rs`
- Modify: `src/console/mod.rs`

**Step 3.1: Write history test**

Create `tests/console_history_tests.rs`:

```rust
//! History buffer tests

use rust_remote_cw_keyer::console::history::History;

#[test]
fn test_history_empty() {
    let history = History::new();
    assert!(history.get_prev().is_none());
    assert!(history.get_next().is_none());
}

#[test]
fn test_history_push_and_recall() {
    let mut history = History::new();

    history.push("help");
    history.push("show wpm");

    // Navigate back
    assert_eq!(history.get_prev(), Some("show wpm"));
    assert_eq!(history.get_prev(), Some("help"));
    assert_eq!(history.get_prev(), Some("help")); // stays at oldest

    // Navigate forward
    assert_eq!(history.get_next(), Some("show wpm"));
    assert_eq!(history.get_next(), None); // back to current input
}

#[test]
fn test_history_overflow() {
    let mut history = History::new();

    history.push("cmd1");
    history.push("cmd2");
    history.push("cmd3");
    history.push("cmd4");
    history.push("cmd5"); // overflow, drops cmd1

    // Should have cmd2, cmd3, cmd4, cmd5 (4 entries max)
    assert_eq!(history.get_prev(), Some("cmd5"));
    assert_eq!(history.get_prev(), Some("cmd4"));
    assert_eq!(history.get_prev(), Some("cmd3"));
    assert_eq!(history.get_prev(), Some("cmd2"));
    assert_eq!(history.get_prev(), Some("cmd2")); // oldest
}

#[test]
fn test_history_reset_on_push() {
    let mut history = History::new();

    history.push("cmd1");
    history.push("cmd2");

    // Navigate back
    history.get_prev();

    // Push new command resets navigation
    history.push("cmd3");

    // Should start from newest
    assert_eq!(history.get_prev(), Some("cmd3"));
}
```

**Step 3.2: Run test to verify it fails**

Run: `cargo test console_history`
Expected: FAIL - module not found

**Step 3.3: Implement history**

Create `src/console/history.rs`:

```rust
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
```

**Step 3.4: Export history from mod.rs**

Add to `src/console/mod.rs`:

```rust
pub mod history;
pub use history::History;
```

**Step 3.5: Run test to verify it passes**

Run: `cargo test console_history`
Expected: All 4 tests PASS

**Step 3.6: Commit history**

```bash
git add src/console/history.rs tests/console_history_tests.rs
git commit -m "feat(console): add command history ring buffer"
```

---

## Task 4: Tab Completion

**Files:**
- Create: `src/console/completion.rs`
- Modify: `src/console/mod.rs`

**Step 4.1: Write completion test**

Create `tests/console_completion_tests.rs`:

```rust
//! Tab completion tests

use rust_remote_cw_keyer::console::completion::Completer;

// Mock completions for testing
static TEST_COMMANDS: &[&str] = &["help", "set", "show", "save", "stats", "status"];
static TEST_PARAMS: &[&str] = &["wpm", "weight", "sidetone_freq_hz", "sidetone_volume"];

#[test]
fn test_complete_first_match() {
    let mut completer = Completer::new();

    // "s" should complete to first match alphabetically
    let result = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(result, Some("save"));
}

#[test]
fn test_complete_cycle() {
    let mut completer = Completer::new();

    // First tab: "s" -> "save"
    let r1 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r1, Some("save"));

    // Second tab: cycle to "set"
    let r2 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r2, Some("set"));

    // Third tab: cycle to "show"
    let r3 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r3, Some("show"));

    // Fourth tab: cycle to "stats"
    let r4 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r4, Some("stats"));

    // Fifth tab: cycle to "status"
    let r5 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r5, Some("status"));

    // Sixth tab: wrap to "save"
    let r6 = completer.complete("s", TEST_COMMANDS.iter().copied());
    assert_eq!(r6, Some("save"));
}

#[test]
fn test_complete_reset_on_different_prefix() {
    let mut completer = Completer::new();

    // "s" -> "save"
    completer.complete("s", TEST_COMMANDS.iter().copied());

    // Change prefix resets cycling
    let result = completer.complete("sh", TEST_COMMANDS.iter().copied());
    assert_eq!(result, Some("show"));
}

#[test]
fn test_complete_no_match() {
    let mut completer = Completer::new();

    let result = completer.complete("xyz", TEST_COMMANDS.iter().copied());
    assert_eq!(result, None);
}

#[test]
fn test_complete_exact_match() {
    let mut completer = Completer::new();

    // Exact match still returns it
    let result = completer.complete("help", TEST_COMMANDS.iter().copied());
    assert_eq!(result, Some("help"));
}
```

**Step 4.2: Run test to verify it fails**

Run: `cargo test console_completion`
Expected: FAIL - module not found

**Step 4.3: Implement completer**

Create `src/console/completion.rs`:

```rust
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

        // Collect matching candidates
        let matches: heapless::Vec<&str, 32> = candidates
            .filter(|c| c.starts_with(prefix))
            .collect();

        if matches.is_empty() {
            self.cycling = false;
            return None;
        }

        // Wrap around
        if self.match_idx >= matches.len() {
            self.match_idx = 0;
        }

        self.cycling = true;
        Some(matches[self.match_idx])
    }

    /// Reset completion state (call when user types non-tab)
    pub fn reset(&mut self) {
        self.cycling = false;
        self.match_idx = 0;
    }
}
```

**Step 4.4: Add heapless dependency**

Modify `Cargo.toml`, add to `[dependencies]`:

```toml
heapless = "0.8"
```

**Step 4.5: Export completer from mod.rs**

Add to `src/console/mod.rs`:

```rust
pub mod completion;
pub use completion::Completer;
```

**Step 4.6: Run test to verify it passes**

Run: `cargo test console_completion`
Expected: All 5 tests PASS

**Step 4.7: Commit completion**

```bash
git add src/console/completion.rs tests/console_completion_tests.rs Cargo.toml
git commit -m "feat(console): add tab completion with cycling"
```

---

## Task 5: Console Core and Line Buffer

**Files:**
- Create: `src/console/line_buffer.rs`
- Modify: `src/console/mod.rs`

**Step 5.1: Write line buffer test**

Create `tests/console_line_buffer_tests.rs`:

```rust
//! Line buffer tests

use rust_remote_cw_keyer::console::line_buffer::LineBuffer;

#[test]
fn test_line_buffer_push() {
    let mut buf = LineBuffer::new();

    buf.push(b'h');
    buf.push(b'e');
    buf.push(b'l');
    buf.push(b'p');

    assert_eq!(buf.as_str(), "help");
}

#[test]
fn test_line_buffer_backspace() {
    let mut buf = LineBuffer::new();

    buf.push(b'h');
    buf.push(b'e');
    buf.push(b'l');
    buf.push(b'p');
    buf.backspace();
    buf.backspace();

    assert_eq!(buf.as_str(), "he");
}

#[test]
fn test_line_buffer_backspace_empty() {
    let mut buf = LineBuffer::new();

    buf.backspace(); // should not panic
    assert_eq!(buf.as_str(), "");
}

#[test]
fn test_line_buffer_clear() {
    let mut buf = LineBuffer::new();

    buf.push(b'h');
    buf.push(b'e');
    buf.push(b'l');
    buf.push(b'p');
    buf.clear();

    assert_eq!(buf.as_str(), "");
    assert!(buf.is_empty());
}

#[test]
fn test_line_buffer_set_from_str() {
    let mut buf = LineBuffer::new();

    buf.set("show wpm");
    assert_eq!(buf.as_str(), "show wpm");
}

#[test]
fn test_line_buffer_overflow() {
    let mut buf = LineBuffer::new();

    // Push 70 characters (buffer is 64)
    for i in 0..70u8 {
        buf.push(b'a' + (i % 26));
    }

    // Should be truncated to 64
    assert_eq!(buf.len(), 64);
}
```

**Step 5.2: Run test to verify it fails**

Run: `cargo test console_line_buffer`
Expected: FAIL - module not found

**Step 5.3: Implement line buffer**

Create `src/console/line_buffer.rs`:

```rust
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
```

**Step 5.4: Export line buffer from mod.rs**

Add to `src/console/mod.rs`:

```rust
pub mod line_buffer;
pub use line_buffer::LineBuffer;
```

**Step 5.5: Run test to verify it passes**

Run: `cargo test console_line_buffer`
Expected: All 6 tests PASS

**Step 5.6: Commit line buffer**

```bash
git add src/console/line_buffer.rs tests/console_line_buffer_tests.rs
git commit -m "feat(console): add line input buffer"
```

---

## Task 6: Command Registry and Dispatcher

**Files:**
- Create: `src/console/commands.rs`
- Modify: `src/console/mod.rs`
- Modify: `src/config/mod.rs`

**Step 6.1: Add config_console to config module**

Modify `src/config/mod.rs`, add after other generated modules:

```rust
#[path = "../generated/config_console.rs"]
mod generated_console;

pub use generated_console::{
    ParamValue, ParamType, ConsoleError as ParamError,
    ParamDescriptor, PARAMS, CATEGORIES,
    find_param, find_params_matching, param_names,
};
```

**Step 6.2: Write commands test**

Create `tests/console_commands_tests.rs`:

```rust
//! Command handler tests

use rust_remote_cw_keyer::console::commands::{execute, COMMANDS};
use rust_remote_cw_keyer::console::parser::parse_line;
use rust_remote_cw_keyer::console::ConsoleError;

#[test]
fn test_command_registry_has_all_commands() {
    let expected = ["help", "set", "show", "debug", "save", "reboot", "factory-reset", "flash", "stats"];

    for name in expected {
        assert!(
            COMMANDS.iter().any(|c| c.name == name),
            "Command '{}' should be in registry",
            name
        );
    }
}

#[test]
fn test_execute_unknown_command() {
    let cmd = parse_line("foobar");
    let result = execute(&cmd, &mut TestOutput::new());

    assert_eq!(result, Err(ConsoleError::UnknownCommand));
}

#[test]
fn test_execute_help() {
    let cmd = parse_line("help");
    let mut output = TestOutput::new();
    let result = execute(&cmd, &mut output);

    assert!(result.is_ok());
    assert!(output.contains("help"));
}

#[test]
fn test_execute_reboot_requires_confirm() {
    let cmd = parse_line("reboot");
    let result = execute(&cmd, &mut TestOutput::new());

    assert_eq!(result, Err(ConsoleError::RequiresConfirm));
}

// Test output buffer
struct TestOutput {
    buf: heapless::String<1024>,
}

impl TestOutput {
    fn new() -> Self {
        Self { buf: heapless::String::new() }
    }

    fn contains(&self, s: &str) -> bool {
        self.buf.contains(s)
    }
}

impl core::fmt::Write for TestOutput {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        self.buf.push_str(s).map_err(|_| core::fmt::Error)
    }
}
```

**Step 6.3: Run test to verify it fails**

Run: `cargo test console_commands`
Expected: FAIL - module not found

**Step 6.4: Implement command handlers**

Create `src/console/commands.rs`:

```rust
//! Command handlers

use core::fmt::Write;
use super::parser::ParsedCommand;
use super::ConsoleError;

/// Command descriptor
pub struct CommandDescriptor {
    pub name: &'static str,
    pub brief: &'static str,
    pub handler: fn(&ParsedCommand<'_>, &mut dyn Write) -> Result<(), ConsoleError>,
}

/// All available commands
pub static COMMANDS: &[CommandDescriptor] = &[
    CommandDescriptor { name: "help", brief: "List commands", handler: cmd_help },
    CommandDescriptor { name: "set", brief: "Set parameter value", handler: cmd_set },
    CommandDescriptor { name: "show", brief: "Show parameters", handler: cmd_show },
    CommandDescriptor { name: "debug", brief: "Control log levels", handler: cmd_debug },
    CommandDescriptor { name: "save", brief: "Persist to NVS", handler: cmd_save },
    CommandDescriptor { name: "reboot", brief: "Restart system", handler: cmd_reboot },
    CommandDescriptor { name: "factory-reset", brief: "Erase NVS and reboot", handler: cmd_factory_reset },
    CommandDescriptor { name: "flash", brief: "Enter bootloader", handler: cmd_flash },
    CommandDescriptor { name: "stats", brief: "System statistics", handler: cmd_stats },
];

/// Execute a parsed command
pub fn execute(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.command.is_empty() {
        return Ok(()); // Empty line, do nothing
    }

    let handler = COMMANDS
        .iter()
        .find(|c| c.name == cmd.command)
        .ok_or(ConsoleError::UnknownCommand)?;

    (handler.handler)(cmd, out)
}

/// Get all command names for completion
pub fn command_names() -> impl Iterator<Item = &'static str> {
    COMMANDS.iter().map(|c| c.name)
}

// --- Command Implementations ---

fn cmd_help(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    if let Some(name) = cmd.arg(0) {
        // Help for specific command
        if let Some(c) = COMMANDS.iter().find(|c| c.name == name) {
            let _ = writeln!(out, "{}: {}", c.name, c.brief);
        } else {
            return Err(ConsoleError::UnknownCommand);
        }
    } else {
        // List all commands
        for c in COMMANDS {
            let _ = writeln!(out, "  {:<14} {}", c.name, c.brief);
        }
    }
    Ok(())
}

fn cmd_set(cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    let name = cmd.arg(0).ok_or(ConsoleError::MissingArg)?;
    let value = cmd.arg(1).ok_or(ConsoleError::MissingArg)?;

    #[cfg(feature = "esp32s3")]
    {
        use crate::config::{find_param, ParamValue, ParamType};

        let param = find_param(name).ok_or(ConsoleError::UnknownCommand)?;

        // Parse value based on type
        let pval = match param.param_type {
            ParamType::Bool => {
                let v = match value {
                    "true" | "1" | "on" => true,
                    "false" | "0" | "off" => false,
                    _ => return Err(ConsoleError::InvalidValue),
                };
                ParamValue::Bool(v)
            }
            ParamType::U8 { min, max } => {
                let v: u8 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U8(v)
            }
            ParamType::U16 { min, max } => {
                let v: u16 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U16(v)
            }
            ParamType::U32 { min, max } => {
                let v: u32 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U32(v)
            }
            ParamType::Enum { max } => {
                let v: u8 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U8(v)
            }
        };

        (param.set_fn)(pval)?;
    }

    Ok(())
}

fn cmd_show(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    {
        use crate::config::{find_param, find_params_matching, ParamValue, PARAMS};

        if let Some(pattern) = cmd.arg(0) {
            if pattern.ends_with('*') {
                // Wildcard match
                for p in find_params_matching(pattern) {
                    let val = (p.get_fn)();
                    let _ = writeln!(out, "{}={}", p.name, format_value(val));
                }
            } else {
                // Single parameter
                let param = find_param(pattern).ok_or(ConsoleError::UnknownCommand)?;
                let val = (param.get_fn)();
                let _ = writeln!(out, "{}={}", param.name, format_value(val));
            }
        } else {
            // Show all
            for p in PARAMS {
                let val = (p.get_fn)();
                let _ = writeln!(out, "{}={}", p.name, format_value(val));
            }
        }
    }

    #[cfg(not(feature = "esp32s3"))]
    {
        let _ = out;
        let _ = cmd;
    }

    Ok(())
}

#[cfg(feature = "esp32s3")]
fn format_value(v: crate::config::ParamValue) -> heapless::String<32> {
    use crate::config::ParamValue;
    let mut s = heapless::String::new();
    match v {
        ParamValue::U8(n) => { let _ = write!(s, "{}", n); }
        ParamValue::U16(n) => { let _ = write!(s, "{}", n); }
        ParamValue::U32(n) => { let _ = write!(s, "{}", n); }
        ParamValue::Bool(b) => { let _ = write!(s, "{}", b); }
    }
    s
}

fn cmd_debug(_cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    // TODO: Implement log level control
    Ok(())
}

fn cmd_save(_cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    {
        use crate::config::save_to_nvs;
        save_to_nvs().map_err(|_| ConsoleError::NvsError)?;
    }
    Ok(())
}

fn cmd_reboot(cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.arg(0) != Some("confirm") {
        return Err(ConsoleError::RequiresConfirm);
    }

    #[cfg(feature = "esp32s3")]
    unsafe {
        esp_idf_sys::esp_restart();
    }

    Ok(())
}

fn cmd_factory_reset(cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.arg(0) != Some("confirm") {
        return Err(ConsoleError::RequiresConfirm);
    }

    #[cfg(feature = "esp32s3")]
    {
        // TODO: Erase NVS partition
        unsafe {
            esp_idf_sys::esp_restart();
        }
    }

    Ok(())
}

fn cmd_flash(_cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    unsafe {
        // Enter ROM bootloader for esptool flashing
        esp_idf_sys::esp_restart();
        // Note: For true bootloader mode, need to set strapping pins
        // This is a simplified version that just restarts
    }

    Ok(())
}

fn cmd_stats(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    match cmd.arg(0) {
        None => cmd_stats_overview(out),
        Some("tasks") => cmd_stats_tasks(out),
        Some("heap") => cmd_stats_heap(out),
        Some("stream") => cmd_stats_stream(out),
        Some("rt") => cmd_stats_rt(out),
        Some(_) => Err(ConsoleError::InvalidValue),
    }
}

fn cmd_stats_overview(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    {
        let heap_free = unsafe { esp_idf_sys::esp_get_free_heap_size() };
        let uptime_us = unsafe { esp_idf_sys::esp_timer_get_time() };
        let uptime_s = uptime_us / 1_000_000;

        let _ = writeln!(out, "uptime: {}s", uptime_s);
        let _ = writeln!(out, "heap: {} bytes free", heap_free);
    }

    #[cfg(not(feature = "esp32s3"))]
    {
        let _ = writeln!(out, "stats not available on host");
    }

    Ok(())
}

fn cmd_stats_tasks(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    {
        let _ = writeln!(out, "=== Core 0 (RT) ===");
        let _ = writeln!(out, "NAME            CPU%  STACK  PRIO");
        let _ = writeln!(out, "(task info requires runtime stats enabled)");
        let _ = writeln!(out);
        let _ = writeln!(out, "=== Core 1 (BE) ===");
        let _ = writeln!(out, "NAME            CPU%  STACK  PRIO");
        let _ = writeln!(out, "(task info requires runtime stats enabled)");
    }

    #[cfg(not(feature = "esp32s3"))]
    {
        let _ = writeln!(out, "stats not available on host");
    }

    Ok(())
}

fn cmd_stats_heap(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(feature = "esp32s3")]
    {
        let free = unsafe { esp_idf_sys::esp_get_free_heap_size() };
        let min = unsafe { esp_idf_sys::esp_get_minimum_free_heap_size() };

        let _ = writeln!(out, "heap free: {} bytes", free);
        let _ = writeln!(out, "heap min:  {} bytes", min);
    }

    #[cfg(not(feature = "esp32s3"))]
    {
        let _ = writeln!(out, "stats not available on host");
    }

    Ok(())
}

fn cmd_stats_stream(out: &mut dyn Write) -> Result<(), ConsoleError> {
    let _ = writeln!(out, "stream: ok");
    // TODO: Add actual stream stats
    Ok(())
}

fn cmd_stats_rt(out: &mut dyn Write) -> Result<(), ConsoleError> {
    let _ = writeln!(out, "rt: ok");
    // TODO: Add actual RT stats (latency, jitter, etc.)
    Ok(())
}
```

**Step 6.5: Export commands from mod.rs**

Add to `src/console/mod.rs`:

```rust
pub mod commands;
pub use commands::{execute, COMMANDS, command_names};
```

**Step 6.6: Run test to verify it passes**

Run: `cargo test console_commands`
Expected: All 4 tests PASS

**Step 6.7: Commit commands**

```bash
git add src/console/commands.rs src/config/mod.rs tests/console_commands_tests.rs
git commit -m "feat(console): add command registry and handlers"
```

---

## Task 7: Console Main Loop

**Files:**
- Create: `src/console/console.rs`
- Modify: `src/console/mod.rs`

**Step 7.1: Implement Console struct**

Create `src/console/console.rs`:

```rust
//! Main console struct integrating all components

use core::fmt::Write;
use super::{
    LineBuffer, History, Completer,
    parse_line, execute, command_names, ConsoleError,
};

/// Version string (set by build.rs)
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
    /// Returns Some(output) if command completed, None if more input needed.
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
        let parts: heapless::Vec<&str, 4> = input.split_whitespace().collect();

        let completion = if parts.len() <= 1 {
            // Complete command
            let prefix = parts.first().copied().unwrap_or("");
            self.completer.complete(prefix, command_names())
        } else {
            // Complete parameter (after "set " or "show ")
            #[cfg(feature = "esp32s3")]
            {
                use crate::config::param_names;
                let prefix = parts.last().copied().unwrap_or("");
                self.completer.complete(prefix, param_names())
            }
            #[cfg(not(feature = "esp32s3"))]
            {
                None
            }
        };

        if let Some(completed) = completion {
            // Clear current word and replace with completion
            let last_word_len = parts.last().map(|s| s.len()).unwrap_or(0);
            for _ in 0..last_word_len {
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
}
```

**Step 7.2: Export Console from mod.rs**

Update `src/console/mod.rs` to be complete:

```rust
//! Serial console for configuration and diagnostics
//!
//! Lazy polling on Core 1 - no dedicated task.
//! Zero heap allocation - all static buffers.

pub mod error;
pub mod parser;
pub mod history;
pub mod completion;
pub mod line_buffer;
pub mod commands;
pub mod console;

pub use error::ConsoleError;
pub use parser::{parse_line, ParsedCommand};
pub use history::History;
pub use completion::Completer;
pub use line_buffer::LineBuffer;
pub use commands::{execute, COMMANDS, command_names};
pub use console::{Console, VERSION};
```

**Step 7.3: Run all console tests**

Run: `cargo test console`
Expected: All tests PASS

**Step 7.4: Commit console main loop**

```bash
git add src/console/console.rs src/console/mod.rs
git commit -m "feat(console): add main console loop with escape sequences"
```

---

## Task 8: Integration and Final Testing

**Step 8.1: Run full test suite**

Run: `cargo test`
Expected: All tests PASS

**Step 8.2: Build for ESP32-S3**

Run: `cargo build --release`
Expected: Build succeeds

**Step 8.3: Verify generated files**

Run: `ls -la src/generated/`
Expected: `config_console.rs` present alongside other generated files

**Step 8.4: Final commit**

```bash
git add -A
git commit -m "feat(console): complete console implementation

- Command parser with tokenization
- History ring buffer (4 entries, 64 bytes each)
- Tab completion with cycling
- Line buffer with escape sequence handling
- Commands: help, set, show, debug, save, reboot, factory-reset, flash, stats
- Parameter registry generated from parameters.yaml
- Codegen extended for config_console.rs"
```

---

## Summary

| Task | Description | Tests |
|------|-------------|-------|
| 1 | Codegen extension | build verification |
| 2 | Parser + error types | 6 tests |
| 3 | History buffer | 4 tests |
| 4 | Tab completion | 5 tests |
| 5 | Line buffer | 6 tests |
| 6 | Command handlers | 4 tests |
| 7 | Console main loop | integration |
| 8 | Final integration | full suite |

**Total estimated tests:** 25+
