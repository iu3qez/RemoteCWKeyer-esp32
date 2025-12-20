//! Audio module integration tests

use rust_remote_cw_keyer::audio::{
    AudioRingBuffer, AudioSource, AudioSourceSelector, FadeState, PttController, PttState,
    SidetoneGen,
};

const SAMPLE_RATE: u32 = 8000;
const FADE_SAMPLES: u16 = 40; // 5ms

/// Simulate the audio pipeline
#[test]
fn test_full_audio_pipeline() {
    // Components
    let mut sidetone = SidetoneGen::new(700, SAMPLE_RATE, FADE_SAMPLES);
    let mut sidetone_buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    let mut ptt = PttController::new(100); // 100ms tail
    let source = AudioSourceSelector::new();

    // Initial state
    assert_eq!(ptt.state(), PttState::Off);
    assert_eq!(source.get(), AudioSource::Silence);
    assert_eq!(sidetone.fade_state(), FadeState::Silent);

    // Simulate key down at t=0
    let key_down = true;
    let mut timestamp_us: u64 = 0;

    // Generate 50 samples (key down)
    for _ in 0..50 {
        let sample = sidetone.next_sample(key_down);
        sidetone_buf.push(sample);

        if sample != 0 {
            ptt.audio_sample(timestamp_us);
        }

        timestamp_us += 125; // 8kHz = 125us per sample
    }

    // PTT should be on
    assert_eq!(ptt.state(), PttState::On);

    // Sidetone should be in sustain (past fade-in)
    assert_eq!(sidetone.fade_state(), FadeState::Sustain);

    // Buffer should have samples
    assert!(!sidetone_buf.is_empty());

    // Simulate key up
    let key_down = false;

    // Generate samples until fade completes
    for _ in 0..100 {
        let sample = sidetone.next_sample(key_down);
        sidetone_buf.push(sample);
        timestamp_us += 125;
        ptt.tick(timestamp_us);
    }

    // Sidetone should be silent
    assert_eq!(sidetone.fade_state(), FadeState::Silent);

    // PTT should still be on (within tail)
    assert_eq!(ptt.state(), PttState::On);

    // Advance time past PTT tail (100ms = 100000us)
    timestamp_us += 100_000;
    ptt.tick(timestamp_us);

    // Now PTT should be off
    assert_eq!(ptt.state(), PttState::Off);
}

/// Test buffer feeding I2S-like consumer
#[test]
fn test_buffer_to_dma_pattern() {
    let mut sidetone = SidetoneGen::new(600, SAMPLE_RATE, 8);
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    // Producer: fill buffer with sidetone
    for _ in 0..32 {
        buf.push(sidetone.next_sample(true));
    }

    assert_eq!(buf.len(), 32);

    // Consumer: read into DMA buffer
    let mut dma_buf = [0i16; 16];
    let read = buf.read_into(&mut dma_buf);

    assert_eq!(read, 16);
    assert_eq!(buf.len(), 16); // 32 - 16 remaining

    // Verify we got actual audio (not all zeros)
    let non_zero = dma_buf.iter().filter(|&&s| s != 0).count();
    assert!(non_zero > 0, "Should have non-zero samples");
}

/// Test source switching
#[test]
fn test_source_switching_with_ptt() {
    let mut ptt = PttController::new(50);
    let source = AudioSourceSelector::new();

    // Key down -> sidetone
    source.set(AudioSource::Sidetone);
    ptt.audio_sample(0);

    assert_eq!(source.get(), AudioSource::Sidetone);
    assert_eq!(ptt.state(), PttState::On);

    // PTT tail expires
    ptt.tick(100_000);
    assert_eq!(ptt.state(), PttState::Off);

    // Switch to remote (PTT off enables remote)
    source.set(AudioSource::Remote);
    assert_eq!(source.get(), AudioSource::Remote);
}
