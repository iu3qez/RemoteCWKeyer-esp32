//! RustRemoteCWKeyer - Main entry point
//!
//! This is a placeholder. The actual implementation will:
//! 1. Initialize hardware (GPIO, I2S, WiFi)
//! 2. Start RT task on Core 0
//! 3. Start background task on Core 1
//! 4. Enter idle loop

#![no_std]
#![no_main]

use esp_idf_sys as _;

use rust_remote_cw_keyer::{
    stream::KeyingStream,
    fault::FaultState,
    consumer::HardRtConsumer,
    iambic::{IambicProcessor, IambicConfig},
    sample::GpioState,
    logging::LogStream,
};

// Static allocations (ARCHITECTURE.md §9.1)
static KEYING_STREAM: KeyingStream = KeyingStream::new();
static FAULT_STATE: FaultState = FaultState::new();
static LOG_STREAM: LogStream = LogStream::new();

#[no_mangle]
fn main() {
    // Initialize ESP-IDF
    esp_idf_sys::link_patches();

    // TODO: Initialize hardware
    // - GPIO for paddle input
    // - GPIO for TX output
    // - I2S for audio output
    // - WiFi for remote operation

    // TODO: Spawn RT task on Core 0
    // - Paddle polling
    // - Iambic FSM
    // - Stream push
    // - Audio/TX consumer

    // TODO: Spawn background task on Core 1
    // - Remote consumer
    // - Decoder consumer
    // - UART log drain

    // Placeholder: simple loop
    loop {
        // Main thread can handle console or go idle
        unsafe {
            esp_idf_sys::vTaskDelay(1000);
        }
    }
}

/// RT task (runs on Core 0, highest priority)
///
/// This is the critical path. Must complete within timing budget.
/// No blocking calls, no allocation, no logging (except rt_log!).
#[allow(dead_code)]
fn rt_task() {
    let mut iambic = IambicProcessor::new(IambicConfig::with_wpm(25));
    let mut consumer = HardRtConsumer::new(&KEYING_STREAM, &FAULT_STATE, 2);

    loop {
        let now_us = timestamp_us();

        // 1. Poll GPIO
        let gpio = poll_gpio();

        // 2. Tick iambic FSM → produce sample
        let sample = iambic.tick(now_us, gpio);

        // 3. Push to stream
        KEYING_STREAM.push(sample);

        // 4. Consume for audio/TX
        match consumer.tick() {
            Ok(Some(sample)) => {
                set_audio(sample.local_key);
                set_tx(sample.local_key);
            }
            Ok(None) => {}
            Err(fault) => {
                set_audio(false);
                set_tx(false);
                // RT-safe log
                rust_remote_cw_keyer::rt_error!(LOG_STREAM, now_us, "FAULT: {:?}", fault);
            }
        }

        // 5. Wait for next tick (target: 50µs = 20kHz)
        delay_until_next_tick();
    }
}

// --- Placeholder functions (to be implemented with real HAL) ---

#[allow(dead_code)]
fn timestamp_us() -> i64 {
    unsafe { esp_idf_sys::esp_timer_get_time() }
}

#[allow(dead_code)]
fn poll_gpio() -> GpioState {
    // TODO: Read actual GPIO pins
    GpioState::new()
}

#[allow(dead_code)]
fn set_audio(_active: bool) {
    // TODO: Control I2S sidetone
}

#[allow(dead_code)]
fn set_tx(_active: bool) {
    // TODO: Control TX GPIO
}

#[allow(dead_code)]
fn delay_until_next_tick() {
    // TODO: Precise timing with hardware timer
    unsafe {
        esp_idf_sys::vTaskDelay(1); // Placeholder
    }
}
