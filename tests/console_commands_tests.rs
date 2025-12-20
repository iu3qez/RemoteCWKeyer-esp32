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
    buf: [u8; 1024],
    len: usize,
}

impl TestOutput {
    fn new() -> Self {
        Self { buf: [0u8; 1024], len: 0 }
    }

    fn contains(&self, s: &str) -> bool {
        if let Ok(content) = core::str::from_utf8(&self.buf[..self.len]) {
            content.contains(s)
        } else {
            false
        }
    }
}

impl core::fmt::Write for TestOutput {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let bytes = s.as_bytes();
        let available = self.buf.len() - self.len;
        let to_copy = bytes.len().min(available);
        self.buf[self.len..self.len + to_copy].copy_from_slice(&bytes[..to_copy]);
        self.len += to_copy;
        Ok(())
    }
}
