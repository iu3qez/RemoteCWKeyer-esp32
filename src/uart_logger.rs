//! UART log output on GPIO6.
//!
//! Provides system logging via UART TX on GPIO6.
//! Requires external USB-UART adapter (CH340, CP2102, etc).
//!
//! # Hardware Setup
//!
//! ```text
//! ESP32-S3 GPIO6 (TX) ──────▶ USB-UART RX
//!                              └─▶ PC Serial Monitor
//! ```
//!
//! **WARNING**: GPIO6 conflicts with Octal PSRAM. Only use on Quad flash boards!

use crate::logging::LogEntry;

#[cfg(not(test))]
use crate::logging::LogLevel;

#[cfg(not(test))]
use crate::{RT_LOG_STREAM, BG_LOG_STREAM};

#[cfg(test)]
use crate::logging::LogLevel;

#[cfg(not(test))]
use esp_idf_svc::hal::gpio;
#[cfg(not(test))]
use esp_idf_svc::hal::uart::{self, UartTxDriver};
#[cfg(not(test))]
use esp_idf_svc::hal::peripheral::Peripheral;

/// UART configuration for logging.
pub struct UartLoggerConfig {
    pub baud_rate: u32,
    pub tx_pin: u8,
}

impl Default for UartLoggerConfig {
    fn default() -> Self {
        Self {
            baud_rate: 115200,
            tx_pin: 6,  // GPIO6 - UART TX (Quad flash, GPIO6 free for UART)
        }
    }
}

/// Format log entry to string.
///
/// Format: `[timestamp_us] LEVEL: message\n`
fn format_log_entry(entry: &LogEntry, buf: &mut [u8]) -> usize {
    use core::fmt::Write;

    struct BufWriter<'a> {
        buf: &'a mut [u8],
        pos: usize,
    }

    impl<'a> Write for BufWriter<'a> {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            let bytes = s.as_bytes();
            let remaining = self.buf.len() - self.pos;
            let to_write = bytes.len().min(remaining);
            self.buf[self.pos..self.pos + to_write].copy_from_slice(&bytes[..to_write]);
            self.pos += to_write;
            Ok(())
        }
    }

    let mut writer = BufWriter { buf, pos: 0 };

    // Format: [timestamp_us] LEVEL: message\n
    let _ = write!(
        writer,
        "[{:10}] {}: {}\n",
        entry.timestamp_us,
        entry.level.as_str(),
        core::str::from_utf8(&entry.msg[..entry.len as usize]).unwrap_or("<invalid utf8>")
    );

    writer.pos
}

/// Initialize UART1 TX-only on GPIO6 for logging output.
///
/// Returns a UartTxDriver configured for TX-only operation.
#[cfg(not(test))]
pub fn init_uart_logger<'d>(
    uart: impl Peripheral<P = esp_idf_svc::hal::uart::UART1> + 'd,
    tx_pin: impl Peripheral<P = impl gpio::OutputPin> + 'd,
    config: &UartLoggerConfig,
) -> Result<UartTxDriver<'d>, esp_idf_svc::sys::EspError> {
    let uart_config = uart::config::Config::default()
        .baudrate(esp_idf_svc::hal::units::Hertz(config.baud_rate));

    UartTxDriver::new(
        uart,
        tx_pin,
        Option::<gpio::AnyIOPin>::None,  // CTS
        Option::<gpio::AnyIOPin>::None,  // RTS
        &uart_config,
    )
}

/// Write a log entry to UART.
#[cfg(not(test))]
pub fn write_log_to_uart(uart: &mut UartTxDriver<'_>, entry: &LogEntry) {
    let mut format_buf = [0u8; 256];
    let len = format_log_entry(entry, &mut format_buf);
    let _ = uart.write(&format_buf[..len]);
}

/// UART log consumer task (runs on Core 1).
///
/// Drains RT_LOG_STREAM and BG_LOG_STREAM, writes to UART.
#[cfg(not(test))]
pub fn uart_logger_task(uart: &mut UartTxDriver<'_>) -> ! {
    let mut format_buf = [0u8; 256];
    let mut last_dropped_report = 0i64;

    loop {
        let mut work_done = false;

        // Priority 1: Drain RT stream (Core 0 - high priority logs)
        while let Some(entry) = RT_LOG_STREAM.drain() {
            let len = format_log_entry(&entry, &mut format_buf);
            let _ = uart.write(&format_buf[..len]);
            work_done = true;
        }

        // Priority 2: Drain background stream (Core 1 - low priority logs)
        while let Some(entry) = BG_LOG_STREAM.drain() {
            let len = format_log_entry(&entry, &mut format_buf);
            let _ = uart.write(&format_buf[..len]);
            work_done = true;
        }

        // Report dropped messages every 10 seconds
        let now = unsafe { esp_idf_svc::sys::esp_timer_get_time() };
        if now - last_dropped_report > 10_000_000 {
            let rt_dropped = RT_LOG_STREAM.dropped();
            let bg_dropped = BG_LOG_STREAM.dropped();

            if rt_dropped > 0 || bg_dropped > 0 {
                use core::fmt::Write;
                let mut msg = [0u8; 64];
                let len = {
                    struct MsgWriter<'a> { buf: &'a mut [u8], pos: usize }
                    impl<'a> Write for MsgWriter<'a> {
                        fn write_str(&mut self, s: &str) -> core::fmt::Result {
                            let bytes = s.as_bytes();
                            let to_write = bytes.len().min(self.buf.len() - self.pos);
                            self.buf[self.pos..self.pos + to_write].copy_from_slice(&bytes[..to_write]);
                            self.pos += to_write;
                            Ok(())
                        }
                    }
                    let mut w = MsgWriter { buf: &mut msg, pos: 0 };
                    let _ = write!(w, "[WARN] Dropped: RT={}, BG={}\n", rt_dropped, bg_dropped);
                    w.pos
                };
                let _ = uart.write(&msg[..len]);

                RT_LOG_STREAM.reset_dropped();
                BG_LOG_STREAM.reset_dropped();
            }

            last_dropped_report = now;
        }

        // If no work, wait before checking again
        if !work_done {
            unsafe {
                esp_idf_svc::sys::vTaskDelay(10);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_log_entry() {
        let entry = LogEntry {
            timestamp_us: 1234567,
            level: LogLevel::Info,
            len: 11,
            msg: {
                let mut msg = [0u8; 120];
                msg[..11].copy_from_slice(b"Hello world");
                msg
            },
        };

        let mut buf = [0u8; 256];
        let len = format_log_entry(&entry, &mut buf);

        let formatted = core::str::from_utf8(&buf[..len]).unwrap();
        assert!(formatted.contains("1234567"));
        assert!(formatted.contains("INFO"));
        assert!(formatted.contains("Hello world"));
    }

    #[test]
    fn test_format_truncated_message() {
        let entry = LogEntry {
            timestamp_us: 999,
            level: LogLevel::Error,
            len: 5,
            msg: {
                let mut msg = [0u8; 120];
                msg[..10].copy_from_slice(b"TEST12345X");  // Only first 5 used
                msg
            },
        };

        let mut buf = [0u8; 256];
        let len = format_log_entry(&entry, &mut buf);

        let formatted = core::str::from_utf8(&buf[..len]).unwrap();
        assert!(formatted.contains("ERROR"));
        assert!(formatted.contains("TEST1"));  // Only 5 chars
        assert!(!formatted.contains("X"));  // 10th char not included
    }
}
