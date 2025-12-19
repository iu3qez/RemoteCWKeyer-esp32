//! RustRemoteCWKeyer - Main entry point
//!
//! This is a placeholder. The actual implementation will:
//! 1. Initialize hardware (GPIO, I2S, WiFi)
//! 2. Start RT task on Core 0
//! 3. Start background task on Core 1
//! 4. Enter idle loop

#![no_std]
#![no_main]

use esp_idf_svc::sys as esp_idf_sys;

use core::cell::UnsafeCell;
use rust_remote_cw_keyer::{
    stream::KeyingStream,
    fault::FaultState,
    consumer::HardRtConsumer,
    iambic::{IambicProcessor, IambicConfig},
    sample::{GpioState, StreamSample},
    logging::LogStream,
};

// Wrapper to make UnsafeCell Sync for static buffers.
// SAFETY: Single producer architecture guarantees no data races.
// Only the RT thread writes, consumers read with proper ordering.
#[repr(transparent)]
struct SyncCell<T>(UnsafeCell<T>);
unsafe impl<T> Sync for SyncCell<T> {}

impl<T> SyncCell<T> {
    const fn new(value: T) -> Self {
        Self(UnsafeCell::new(value))
    }

    fn as_unsafe_cell(&self) -> &UnsafeCell<T> {
        &self.0
    }
}

// Static buffer for KeyingStream (ARCHITECTURE.md §9.1.4: PSRAM for stream buffer)
// In production: allocate in PSRAM. For now: small static buffer.
// Size must be power of 2.
const STREAM_BUFFER_SIZE: usize = 4096;
static STREAM_BUFFER: [SyncCell<StreamSample>; STREAM_BUFFER_SIZE] = {
    const EMPTY_CELL: SyncCell<StreamSample> = SyncCell::new(StreamSample::EMPTY);
    [EMPTY_CELL; STREAM_BUFFER_SIZE]
};

// Static allocations (ARCHITECTURE.md §9.1)
// Note: KeyingStream must be initialized at runtime with the buffer reference
static mut KEYING_STREAM: Option<KeyingStream> = None;
static FAULT_STATE: FaultState = FaultState::new();
static LOG_STREAM: LogStream = LogStream::new();

/// Initialize the keying stream (call once at startup)
fn init_keying_stream() -> &'static KeyingStream {
    // SAFETY: SyncCell<T> is #[repr(transparent)] wrapper around UnsafeCell<T>,
    // so &[SyncCell<T>] has the same layout as &[UnsafeCell<T>].
    let buffer: &'static [UnsafeCell<StreamSample>] = unsafe {
        core::mem::transmute::<
            &'static [SyncCell<StreamSample>],
            &'static [UnsafeCell<StreamSample>]
        >(&STREAM_BUFFER[..])
    };

    unsafe {
        KEYING_STREAM = Some(KeyingStream::with_buffer(buffer));
        KEYING_STREAM.as_ref().unwrap()
    }
}

#[no_mangle]
fn main() {
    // Initialize ESP-IDF
    esp_idf_sys::link_patches();

    // Initialize keying stream
    let _stream = init_keying_stream();

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
    // Get the initialized stream
    let stream = unsafe { KEYING_STREAM.as_ref().expect("stream not initialized") };

    let mut iambic = IambicProcessor::new(IambicConfig::with_wpm(25));
    let mut consumer = HardRtConsumer::new(stream, &FAULT_STATE, 2);

    loop {
        let now_us = timestamp_us();

        // 1. Poll GPIO
        let gpio = poll_gpio();

        // 2. Tick iambic FSM → produce sample
        let sample = iambic.tick(now_us, gpio);

        // 3. Push to stream
        stream.push(sample);

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
    GpioState::IDLE
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
