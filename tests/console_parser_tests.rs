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
