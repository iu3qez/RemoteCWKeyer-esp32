//! History buffer tests

use rust_remote_cw_keyer::console::history::History;

#[test]
fn test_history_empty() {
    let mut history = History::new();
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
