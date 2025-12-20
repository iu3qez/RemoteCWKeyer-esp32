//! Audio source selector tests

use rust_remote_cw_keyer::audio::source::{AudioSource, AudioSourceSelector};

#[test]
fn test_source_initial_state() {
    let selector = AudioSourceSelector::new();
    assert_eq!(selector.get(), AudioSource::Silence);
}

#[test]
fn test_source_set_sidetone() {
    let selector = AudioSourceSelector::new();
    selector.set(AudioSource::Sidetone);
    assert_eq!(selector.get(), AudioSource::Sidetone);
}

#[test]
fn test_source_set_remote() {
    let selector = AudioSourceSelector::new();
    selector.set(AudioSource::Remote);
    assert_eq!(selector.get(), AudioSource::Remote);
}

#[test]
fn test_source_atomic_update() {
    use std::sync::Arc;
    use std::thread;

    let selector = Arc::new(AudioSourceSelector::new());

    // Spawn multiple threads setting different values
    let handles: Vec<_> = (0..10)
        .map(|i| {
            let sel = Arc::clone(&selector);
            thread::spawn(move || {
                for _ in 0..100 {
                    if i % 2 == 0 {
                        sel.set(AudioSource::Sidetone);
                    } else {
                        sel.set(AudioSource::Remote);
                    }
                }
            })
        })
        .collect();

    for h in handles {
        h.join().unwrap();
    }

    // Should be one of the valid states (not corrupted)
    let state = selector.get();
    assert!(
        state == AudioSource::Silence
            || state == AudioSource::Sidetone
            || state == AudioSource::Remote
    );
}
