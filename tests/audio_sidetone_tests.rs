//! Sidetone generator tests

use rust_remote_cw_keyer::audio::sidetone::{SidetoneGen, FadeState};

const SAMPLE_RATE: u32 = 8000;

#[test]
fn test_sidetone_gen_creation() {
    let gen = SidetoneGen::new(700, SAMPLE_RATE, 40); // 700Hz, 8kHz, 5ms fade (40 samples)
    assert_eq!(gen.fade_state(), FadeState::Silent);
}

#[test]
fn test_sidetone_silent_when_key_up() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Generate samples with key up
    for _ in 0..100 {
        let sample = gen.next_sample(false);
        assert_eq!(sample, 0, "Should be silent when key up");
    }
}

#[test]
fn test_sidetone_produces_audio_when_key_down() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Key down, skip fade-in period
    for _ in 0..50 {
        gen.next_sample(true);
    }

    // Now should be producing audio
    let sample = gen.next_sample(true);
    assert_ne!(sample, 0, "Should produce audio when key down after fade-in");
}

#[test]
fn test_sidetone_fade_in() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // First sample should be near zero (start of fade)
    let first = gen.next_sample(true).abs();

    // Skip to end of fade
    for _ in 0..38 {
        gen.next_sample(true);
    }

    // Last fade sample should be louder
    let last = gen.next_sample(true).abs();

    // After fade, should be at full amplitude
    gen.next_sample(true);
    assert_eq!(gen.fade_state(), FadeState::Sustain);
}

#[test]
fn test_sidetone_fade_out() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Key down, complete fade-in
    for _ in 0..50 {
        gen.next_sample(true);
    }
    assert_eq!(gen.fade_state(), FadeState::Sustain);

    // Key up, should start fade-out
    gen.next_sample(false);
    assert_eq!(gen.fade_state(), FadeState::FadeOut);

    // Complete fade-out
    for _ in 0..50 {
        gen.next_sample(false);
    }
    assert_eq!(gen.fade_state(), FadeState::Silent);
}

#[test]
fn test_sidetone_frequency_accuracy() {
    let mut gen = SidetoneGen::new(800, SAMPLE_RATE, 8); // 800 Hz, minimal fade

    // Key down, skip fade
    for _ in 0..10 {
        gen.next_sample(true);
    }

    // At 800 Hz and 8000 sample rate, one cycle = 10 samples
    // Collect one full cycle worth of samples
    let mut samples = Vec::new();
    for _ in 0..10 {
        samples.push(gen.next_sample(true));
    }

    // Should have both positive and negative values (completed a cycle)
    let has_positive = samples.iter().any(|&s| s > 1000);
    let has_negative = samples.iter().any(|&s| s < -1000);
    assert!(has_positive && has_negative, "Should complete a cycle in 10 samples");
}

#[test]
fn test_sidetone_phase_continuity() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 8);

    // Key down
    for _ in 0..20 {
        gen.next_sample(true);
    }

    // Get two consecutive samples
    let s1 = gen.next_sample(true);
    let s2 = gen.next_sample(true);

    // Difference should be small (no phase jumps)
    let diff = (s2 as i32 - s1 as i32).abs();
    assert!(diff < 5000, "Phase should be continuous, diff={}", diff);
}
