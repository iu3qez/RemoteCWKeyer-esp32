//! Console error types

/// Console error with code and message
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConsoleError {
    /// E01: Unknown command
    UnknownCommand,
    /// E02: Invalid value format
    InvalidValue,
    /// E03: Missing required argument
    MissingArg,
    /// E04: Value out of allowed range
    OutOfRange,
    /// E05: Dangerous command requires 'confirm'
    RequiresConfirm,
    /// E06: NVS storage error
    NvsError,
}

impl ConsoleError {
    /// Get error code string
    pub fn code(&self) -> &'static str {
        match self {
            Self::UnknownCommand => "E01",
            Self::InvalidValue => "E02",
            Self::MissingArg => "E03",
            Self::OutOfRange => "E04",
            Self::RequiresConfirm => "E05",
            Self::NvsError => "E06",
        }
    }

    /// Get error message
    pub fn message(&self) -> &'static str {
        match self {
            Self::UnknownCommand => "unknown command",
            Self::InvalidValue => "invalid value",
            Self::MissingArg => "missing argument",
            Self::OutOfRange => "out of range",
            Self::RequiresConfirm => "requires 'confirm'",
            Self::NvsError => "NVS error",
        }
    }
}

impl core::fmt::Display for ConsoleError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "{}: {}", self.code(), self.message())
    }
}
