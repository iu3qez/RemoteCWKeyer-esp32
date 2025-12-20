//! Command handlers

use core::fmt::Write;
use super::parser::ParsedCommand;
use super::ConsoleError;

/// Command descriptor
pub struct CommandDescriptor {
    pub name: &'static str,
    pub brief: &'static str,
    pub handler: fn(&ParsedCommand<'_>, &mut dyn Write) -> Result<(), ConsoleError>,
}

/// All available commands
pub static COMMANDS: &[CommandDescriptor] = &[
    CommandDescriptor { name: "help", brief: "List commands", handler: cmd_help },
    CommandDescriptor { name: "set", brief: "Set parameter value", handler: cmd_set },
    CommandDescriptor { name: "show", brief: "Show parameters", handler: cmd_show },
    CommandDescriptor { name: "debug", brief: "Control log levels", handler: cmd_debug },
    CommandDescriptor { name: "save", brief: "Persist to NVS", handler: cmd_save },
    CommandDescriptor { name: "reboot", brief: "Restart system", handler: cmd_reboot },
    CommandDescriptor { name: "factory-reset", brief: "Erase NVS and reboot", handler: cmd_factory_reset },
    CommandDescriptor { name: "flash", brief: "Enter bootloader", handler: cmd_flash },
    CommandDescriptor { name: "stats", brief: "System statistics", handler: cmd_stats },
];

/// Execute a parsed command
pub fn execute(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.command.is_empty() {
        return Ok(()); // Empty line, do nothing
    }

    let handler = COMMANDS
        .iter()
        .find(|c| c.name == cmd.command)
        .ok_or(ConsoleError::UnknownCommand)?;

    (handler.handler)(cmd, out)
}

/// Get all command names for completion
pub fn command_names() -> impl Iterator<Item = &'static str> {
    COMMANDS.iter().map(|c| c.name)
}

// --- Command Implementations ---

fn cmd_help(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    if let Some(name) = cmd.arg(0) {
        // Help for specific command
        if let Some(c) = COMMANDS.iter().find(|c| c.name == name) {
            let _ = writeln!(out, "{}: {}", c.name, c.brief);
        } else {
            return Err(ConsoleError::UnknownCommand);
        }
    } else {
        // List all commands
        for c in COMMANDS {
            let _ = writeln!(out, "  {:<14} {}", c.name, c.brief);
        }
    }
    Ok(())
}

fn cmd_set(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    let name = cmd.arg(0).ok_or(ConsoleError::MissingArg)?;
    let value = cmd.arg(1).ok_or(ConsoleError::MissingArg)?;

    #[cfg(not(test))]
    {
        use crate::config::{find_param, ParamValue, ParamType};

        let param = find_param(name).ok_or(ConsoleError::UnknownCommand)?;

        // Parse value based on type
        let pval = match param.param_type {
            ParamType::Bool => {
                let v = match value {
                    "true" | "1" | "on" => true,
                    "false" | "0" | "off" => false,
                    _ => return Err(ConsoleError::InvalidValue),
                };
                ParamValue::Bool(v)
            }
            ParamType::U8 { min, max } => {
                let v: u8 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U8(v)
            }
            ParamType::U16 { min, max } => {
                let v: u16 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U16(v)
            }
            ParamType::U32 { min, max } => {
                let v: u32 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v < min || v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U32(v)
            }
            ParamType::Enum { max } => {
                let v: u8 = value.parse().map_err(|_| ConsoleError::InvalidValue)?;
                if v > max {
                    return Err(ConsoleError::OutOfRange);
                }
                ParamValue::U8(v)
            }
        };

        (param.set_fn)(pval).map_err(|_| ConsoleError::InvalidValue)?;
        let _ = writeln!(out, "{}={}", name, value);
    }

    #[cfg(test)]
    {
        // In test mode, just acknowledge
        let _ = writeln!(out, "{}={}", name, value);
    }

    Ok(())
}

fn cmd_show(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(not(test))]
    {
        use crate::config::{find_param, find_params_matching, ParamValue, PARAMS};

        if let Some(pattern) = cmd.arg(0) {
            if pattern.ends_with('*') {
                // Wildcard match
                for p in find_params_matching(pattern) {
                    let val = (p.get_fn)();
                    let _ = writeln!(out, "{}={}", p.name, format_value(val));
                }
            } else {
                // Single parameter
                let param = find_param(pattern).ok_or(ConsoleError::UnknownCommand)?;
                let val = (param.get_fn)();
                let _ = writeln!(out, "{}={}", param.name, format_value(val));
            }
        } else {
            // Show all
            for p in PARAMS {
                let val = (p.get_fn)();
                let _ = writeln!(out, "{}={}", p.name, format_value(val));
            }
        }
    }

    #[cfg(test)]
    {
        // In test mode, show placeholder
        if let Some(pattern) = cmd.arg(0) {
            let _ = writeln!(out, "show {}", pattern);
        } else {
            let _ = writeln!(out, "show all");
        }
    }

    Ok(())
}

#[cfg(not(test))]
fn format_value(v: crate::config::ParamValue) -> FormatBuffer {
    use crate::config::ParamValue;
    let mut buf = FormatBuffer::new();
    match v {
        ParamValue::U8(n) => { let _ = write!(buf, "{}", n); }
        ParamValue::U16(n) => { let _ = write!(buf, "{}", n); }
        ParamValue::U32(n) => { let _ = write!(buf, "{}", n); }
        ParamValue::Bool(b) => { let _ = write!(buf, "{}", b); }
    }
    buf
}

/// Small formatting buffer for values
#[cfg(not(test))]
struct FormatBuffer {
    buf: [u8; 32],
    len: usize,
}

#[cfg(not(test))]
impl FormatBuffer {
    fn new() -> Self {
        Self { buf: [0u8; 32], len: 0 }
    }
}

#[cfg(not(test))]
impl core::fmt::Write for FormatBuffer {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let bytes = s.as_bytes();
        let available = self.buf.len() - self.len;
        let to_copy = bytes.len().min(available);
        self.buf[self.len..self.len + to_copy].copy_from_slice(&bytes[..to_copy]);
        self.len += to_copy;
        Ok(())
    }
}

#[cfg(not(test))]
impl core::fmt::Display for FormatBuffer {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        if let Ok(s) = core::str::from_utf8(&self.buf[..self.len]) {
            f.write_str(s)
        } else {
            Ok(())
        }
    }
}

fn cmd_debug(_cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    // TODO: Implement log level control
    let _ = writeln!(out, "debug: not yet implemented");
    Ok(())
}

fn cmd_save(_cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(not(test))]
    {
        use crate::config::save_to_nvs;
        save_to_nvs().map_err(|_| ConsoleError::NvsError)?;
        let _ = writeln!(out, "saved");
    }

    #[cfg(test)]
    {
        let _ = writeln!(out, "saved");
    }

    Ok(())
}

fn cmd_reboot(cmd: &ParsedCommand<'_>, _out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.arg(0) != Some("confirm") {
        return Err(ConsoleError::RequiresConfirm);
    }

    #[cfg(all(not(test), target_arch = "xtensa"))]
    unsafe {
        esp_idf_sys::esp_restart();
    }

    Ok(())
}

fn cmd_factory_reset(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    if cmd.arg(0) != Some("confirm") {
        return Err(ConsoleError::RequiresConfirm);
    }

    let _ = writeln!(out, "Erasing NVS and restarting...");

    #[cfg(all(not(test), target_arch = "xtensa"))]
    unsafe {
        // Erase the default NVS partition
        // This de-initializes NVS if initialized, then erases all contents
        // See: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
        let err = esp_idf_sys::nvs_flash_erase();
        if err != esp_idf_sys::ESP_OK {
            // Log error but continue with restart anyway
            // NVS will be re-initialized on next boot
        }

        esp_idf_sys::esp_restart();
    }

    Ok(())
}

fn cmd_flash(_cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    let _ = writeln!(out, "Entering download mode...");

    #[cfg(all(not(test), target_arch = "xtensa"))]
    unsafe {
        // ESP32-S3 supports forcing download boot via RTC register
        // REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT)
        //
        // RTC_CNTL_OPTION1_REG = 0x6000_8128 (for S3)
        // RTC_CNTL_FORCE_DOWNLOAD_BOOT = BIT(0)
        //
        // Note: May have issues with USB/TinyUSB on S3.
        // See: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html

        const RTC_CNTL_OPTION1_REG: u32 = 0x6000_8128;
        const RTC_CNTL_FORCE_DOWNLOAD_BOOT: u32 = 1;

        core::ptr::write_volatile(RTC_CNTL_OPTION1_REG as *mut u32, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_idf_sys::esp_restart();
    }

    Ok(())
}

fn cmd_stats(cmd: &ParsedCommand<'_>, out: &mut dyn Write) -> Result<(), ConsoleError> {
    match cmd.arg(0) {
        None => cmd_stats_overview(out),
        Some("tasks") => cmd_stats_tasks(out),
        Some("heap") => cmd_stats_heap(out),
        Some("stream") => cmd_stats_stream(out),
        Some("rt") => cmd_stats_rt(out),
        Some(_) => Err(ConsoleError::InvalidValue),
    }
}

fn cmd_stats_overview(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(all(not(test), target_arch = "xtensa"))]
    {
        let heap_free = unsafe { esp_idf_sys::esp_get_free_heap_size() };
        let uptime_us = unsafe { esp_idf_sys::esp_timer_get_time() };
        let uptime_s = uptime_us / 1_000_000;

        let _ = writeln!(out, "uptime: {}s", uptime_s);
        let _ = writeln!(out, "heap: {} bytes free", heap_free);
    }

    #[cfg(any(test, not(target_arch = "xtensa")))]
    {
        let _ = writeln!(out, "stats: running on host");
    }

    Ok(())
}

fn cmd_stats_tasks(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(all(not(test), target_arch = "xtensa"))]
    {
        let _ = writeln!(out, "=== Core 0 (RT) ===");
        let _ = writeln!(out, "NAME            CPU%  STACK  PRIO");
        let _ = writeln!(out, "(task info requires runtime stats enabled)");
        let _ = writeln!(out);
        let _ = writeln!(out, "=== Core 1 (BE) ===");
        let _ = writeln!(out, "NAME            CPU%  STACK  PRIO");
        let _ = writeln!(out, "(task info requires runtime stats enabled)");
    }

    #[cfg(any(test, not(target_arch = "xtensa")))]
    {
        let _ = writeln!(out, "stats tasks: running on host");
    }

    Ok(())
}

fn cmd_stats_heap(out: &mut dyn Write) -> Result<(), ConsoleError> {
    #[cfg(all(not(test), target_arch = "xtensa"))]
    {
        let free = unsafe { esp_idf_sys::esp_get_free_heap_size() };
        let min = unsafe { esp_idf_sys::esp_get_minimum_free_heap_size() };

        let _ = writeln!(out, "heap free: {} bytes", free);
        let _ = writeln!(out, "heap min:  {} bytes", min);
    }

    #[cfg(any(test, not(target_arch = "xtensa")))]
    {
        let _ = writeln!(out, "stats heap: running on host");
    }

    Ok(())
}

fn cmd_stats_stream(out: &mut dyn Write) -> Result<(), ConsoleError> {
    let _ = writeln!(out, "stream: ok");
    // TODO: Add actual stream stats
    Ok(())
}

fn cmd_stats_rt(out: &mut dyn Write) -> Result<(), ConsoleError> {
    let _ = writeln!(out, "rt: ok");
    // TODO: Add actual RT stats (latency, jitter, etc.)
    Ok(())
}
