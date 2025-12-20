//! UART log consumer thread.
//!
//! Drains RT_LOG_STREAM and BG_LOG_STREAM, writes to UART.
//! Runs on Core 1 at low priority.
//!
//! # Architecture
//!
//! ```text
//! RT_LOG_STREAM (Core 0) ─┐
//!                         ├─▶ UART Consumer ─▶ Serial Output
//! BG_LOG_STREAM (Core 1) ─┘    (Core 1)
//! ```

use crate::logging::LogEntry;

#[cfg(not(test))]
use crate::logging::LogLevel;

#[cfg(not(test))]
use crate::{RT_LOG_STREAM, BG_LOG_STREAM};

#[cfg(test)]
use crate::logging::LogLevel;

/// UART configuration for logging.
pub struct UartLoggerConfig {
    pub baud_rate: u32,
    pub tx_pin: u8,
}

impl Default for UartLoggerConfig {
    fn default() -> Self {
        Self {
            baud_rate: 115200,
            tx_pin: 43,  // Default per ESP32-S3
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

/// UART log consumer task (runs on Core 1).
///
/// # Implementation Notes
///
/// This is a placeholder implementation. Full UART integration requires:
/// - esp_idf_hal::uart::UartDriver for actual UART communication
/// - Proper initialization and error handling
/// - Core affinity setting via vTaskCoreAffinitySet
///
/// For now, this provides the framework and logic.
#[cfg(not(test))]
pub fn uart_logger_task(_config: UartLoggerConfig) -> ! {
    // TODO: Initialize UART with esp-idf-hal
    // let peripherals = unsafe { esp_idf_hal::peripherals::Peripherals::take() };
    // let uart = UartDriver::new(peripherals.uart0, ...);

    let mut format_buf = [0u8; 256];
    let mut last_dropped_report = 0i64;

    loop {
        let mut work_done = false;

        // Priorità 1: Drena RT stream (Core 0 - alta priorità)
        while let Some(entry) = RT_LOG_STREAM.drain() {
            let len = format_log_entry(&entry, &mut format_buf);
            // TODO: uart.write(&format_buf[..len])
            // For now: just format to verify logic
            let _ = len;
            work_done = true;
        }

        // Priorità 2: Drena background stream (Core 1 - bassa priorità)
        while let Some(entry) = BG_LOG_STREAM.drain() {
            let len = format_log_entry(&entry, &mut format_buf);
            // TODO: uart.write(&format_buf[..len])
            let _ = len;
            work_done = true;
        }

        // Report dropped messages ogni 10 secondi
        let now = unsafe { esp_idf_svc::sys::esp_timer_get_time() };
        if now - last_dropped_report > 10_000_000 {
            let rt_dropped = RT_LOG_STREAM.dropped();
            let bg_dropped = BG_LOG_STREAM.dropped();

            if rt_dropped > 0 || bg_dropped > 0 {
                // TODO: Format and write dropped message report
                // let msg = format!("[WARNING] Dropped logs - RT: {}, BG: {}\n", ...);
                // uart.write(msg.as_bytes());

                RT_LOG_STREAM.reset_dropped();
                BG_LOG_STREAM.reset_dropped();
            }

            last_dropped_report = now;
        }

        // Se non c'è lavoro, aspetta prima di controllare di nuovo
        if !work_done {
            // TODO: Use proper delay (FreeRtos::delay_ms(10))
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
