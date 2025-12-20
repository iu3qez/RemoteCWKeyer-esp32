//! Audio ring buffer tests

use rust_remote_cw_keyer::audio::buffer::AudioRingBuffer;

#[test]
fn test_buffer_empty() {
    let buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    assert!(buf.is_empty());
    assert_eq!(buf.len(), 0);
}

#[test]
fn test_buffer_push_pop() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    buf.push(100);
    buf.push(200);
    buf.push(300);

    assert_eq!(buf.len(), 3);
    assert_eq!(buf.pop(), Some(100));
    assert_eq!(buf.pop(), Some(200));
    assert_eq!(buf.pop(), Some(300));
    assert_eq!(buf.pop(), None);
}

#[test]
fn test_buffer_wrap_around() {
    let mut buf: AudioRingBuffer<4> = AudioRingBuffer::new();

    // Fill buffer
    buf.push(1);
    buf.push(2);
    buf.push(3);
    buf.push(4);

    // Pop some
    assert_eq!(buf.pop(), Some(1));
    assert_eq!(buf.pop(), Some(2));

    // Push more (wraps around)
    buf.push(5);
    buf.push(6);

    // Should get remaining in order
    assert_eq!(buf.pop(), Some(3));
    assert_eq!(buf.pop(), Some(4));
    assert_eq!(buf.pop(), Some(5));
    assert_eq!(buf.pop(), Some(6));
}

#[test]
fn test_buffer_overflow_drops_oldest() {
    let mut buf: AudioRingBuffer<4> = AudioRingBuffer::new();

    // Overfill
    buf.push(1);
    buf.push(2);
    buf.push(3);
    buf.push(4);
    buf.push(5); // Overwrites 1
    buf.push(6); // Overwrites 2

    // Should get 3,4,5,6 (oldest dropped)
    assert_eq!(buf.pop(), Some(3));
    assert_eq!(buf.pop(), Some(4));
    assert_eq!(buf.pop(), Some(5));
    assert_eq!(buf.pop(), Some(6));
}

#[test]
fn test_buffer_read_into_slice() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    for i in 0..10i16 {
        buf.push(i * 100);
    }

    let mut output = [0i16; 5];
    let read = buf.read_into(&mut output);

    assert_eq!(read, 5);
    assert_eq!(output, [0, 100, 200, 300, 400]);

    // Buffer should have 5 remaining
    assert_eq!(buf.len(), 5);
}

#[test]
fn test_buffer_read_into_underrun() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    buf.push(100);
    buf.push(200);

    let mut output = [0i16; 5];
    let read = buf.read_into(&mut output);

    assert_eq!(read, 2);
    assert_eq!(output[0], 100);
    assert_eq!(output[1], 200);
    // Rest should be zeroed
    assert_eq!(output[2], 0);
    assert_eq!(output[3], 0);
    assert_eq!(output[4], 0);
}

#[test]
fn test_buffer_available() {
    let buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    assert_eq!(buf.available(), 64);

    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    for i in 0..10 {
        buf.push(i);
    }
    assert_eq!(buf.available(), 54);
}
