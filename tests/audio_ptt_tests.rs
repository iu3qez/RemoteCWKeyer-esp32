//! PTT state machine tests

use rust_remote_cw_keyer::audio::ptt::{PttState, PttController};

// Mock microsecond time for testing
fn make_ptt(tail_ms: u32) -> PttController {
    PttController::new(tail_ms)
}

#[test]
fn test_ptt_initial_state() {
    let ptt = make_ptt(100);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_activates_on_audio() {
    let mut ptt = make_ptt(100);

    // First audio sample triggers PTT
    ptt.audio_sample(1000); // timestamp 1000us
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_stays_on_during_audio() {
    let mut ptt = make_ptt(100);

    ptt.audio_sample(1000);
    assert_eq!(ptt.state(), PttState::On);

    ptt.audio_sample(2000);
    assert_eq!(ptt.state(), PttState::On);

    ptt.audio_sample(3000);
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_tail_timeout() {
    let mut ptt = make_ptt(100); // 100ms tail

    // Start audio at t=0
    ptt.audio_sample(0);
    assert_eq!(ptt.state(), PttState::On);

    // Last audio at t=1000us (1ms)
    ptt.audio_sample(1000);

    // Check at t=50ms: should still be on (within tail)
    ptt.tick(50_000);
    assert_eq!(ptt.state(), PttState::On);

    // Check at t=101ms: should be off (tail expired)
    // 1000us (last audio) + 100_000us (tail) = 101_000us
    ptt.tick(102_000);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_tail_reset_on_new_audio() {
    let mut ptt = make_ptt(100); // 100ms tail

    // Audio at t=0
    ptt.audio_sample(0);

    // Check at t=50ms: still on
    ptt.tick(50_000);
    assert_eq!(ptt.state(), PttState::On);

    // New audio at t=80ms resets tail
    ptt.audio_sample(80_000);

    // Check at t=150ms: should still be on (80ms + 100ms = 180ms)
    ptt.tick(150_000);
    assert_eq!(ptt.state(), PttState::On);

    // Check at t=181ms: should be off
    ptt.tick(181_000);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_immediate_on() {
    let mut ptt = make_ptt(100);

    // Should be off
    assert_eq!(ptt.state(), PttState::Off);

    // Audio triggers immediate on (no delay)
    ptt.audio_sample(5000);
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_reactivation() {
    let mut ptt = make_ptt(50); // 50ms tail

    // First activation
    ptt.audio_sample(0);
    assert_eq!(ptt.state(), PttState::On);

    // Expire
    ptt.tick(60_000);
    assert_eq!(ptt.state(), PttState::Off);

    // Reactivate
    ptt.audio_sample(70_000);
    assert_eq!(ptt.state(), PttState::On);
}
