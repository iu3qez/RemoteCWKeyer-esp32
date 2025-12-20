//! Tab completion tests

use rust_remote_cw_keyer::console::completion::Completer;

// Mock completions for testing
static TEST_COMMANDS: &[&str] = &["help", "set", "show", "save", "stats", "status"];

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
