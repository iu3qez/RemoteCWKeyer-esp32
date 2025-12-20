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
