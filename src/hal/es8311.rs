//! ES8311 audio codec driver
//!
//! I2C control + I2S audio interface.
//! Reference: ES8311 datasheet

/// ES8311 I2C address (depends on AD0 pin)
pub const ES8311_ADDR: u8 = 0x18; // AD0 = LOW

/// ES8311 register addresses
#[allow(dead_code)]
mod regs {
    pub const RESET: u8 = 0x00;
    pub const CLK_MANAGER1: u8 = 0x01;
    pub const CLK_MANAGER2: u8 = 0x02;
    pub const CLK_MANAGER3: u8 = 0x03;
    pub const CLK_MANAGER4: u8 = 0x04;
    pub const CLK_MANAGER5: u8 = 0x05;
    pub const CLK_MANAGER6: u8 = 0x06;
    pub const CLK_MANAGER7: u8 = 0x07;
    pub const CLK_MANAGER8: u8 = 0x08;
    pub const SDP_IN: u8 = 0x09;
    pub const SDP_OUT: u8 = 0x0A;
    pub const SYSTEM: u8 = 0x0B;
    pub const SYS_MODSEL: u8 = 0x0D;
    pub const ADC_MUTE: u8 = 0x0F;
    pub const ADC_VOL: u8 = 0x10;
    pub const ADC_ALC1: u8 = 0x11;
    pub const ADC_ALC2: u8 = 0x12;
    pub const ADC_ALC3: u8 = 0x13;
    pub const ADC_ALC4: u8 = 0x14;
    pub const ADC_ALC5: u8 = 0x15;
    pub const ADC_ALC6: u8 = 0x16;
    pub const DAC_MUTE: u8 = 0x17;
    pub const DAC_VOL: u8 = 0x18;
    pub const DAC_DEM: u8 = 0x19;
    pub const DAC_RAMP: u8 = 0x1A;
    pub const ADC_HPF1: u8 = 0x1B;
    pub const ADC_HPF2: u8 = 0x1C;
    pub const GPIO: u8 = 0x44;
    pub const GP_REG: u8 = 0x45;
    pub const CHIP_ID1: u8 = 0xFD;
    pub const CHIP_ID2: u8 = 0xFE;
    pub const CHIP_VER: u8 = 0xFF;
}

/// ES8311 configuration
#[derive(Debug, Clone)]
pub struct Es8311Config {
    /// Sample rate in Hz
    pub sample_rate: u32,
    /// Initial DAC volume (0-100%)
    pub volume: u8,
}

impl Default for Es8311Config {
    fn default() -> Self {
        Self {
            sample_rate: 8000,
            volume: 70,
        }
    }
}

/// ES8311 driver error
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Es8311Error {
    /// I2C communication error
    I2cError,
    /// Invalid configuration
    InvalidConfig,
    /// Chip not responding or wrong ID
    ChipNotFound,
}

/// ES8311 driver (placeholder for ESP32 implementation)
///
/// On host: provides interface for testing
/// On ESP32: full I2C/I2S implementation
pub struct Es8311 {
    config: Es8311Config,
    volume: u8,
    muted: bool,
}

impl Es8311 {
    /// Create new ES8311 driver
    pub fn new(config: Es8311Config) -> Self {
        Self {
            volume: config.volume,
            config,
            muted: false,
        }
    }

    /// Initialize the codec
    ///
    /// On ESP32: performs full I2C initialization sequence
    #[cfg(not(feature = "esp32s3"))]
    pub fn init(&mut self) -> Result<(), Es8311Error> {
        // Host: no-op
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn init(&mut self, i2c: &mut impl embedded_hal::i2c::I2c) -> Result<(), Es8311Error> {
        use regs::*;

        // Soft reset
        self.write_reg(i2c, RESET, 0x80)?;

        // Wait for reset (should use delay)
        for _ in 0..1000 {
            core::hint::spin_loop();
        }

        // Configure for 8kHz, I2S slave mode
        // Clock configuration for MCLK = 256 * fs
        self.write_reg(i2c, CLK_MANAGER1, 0x3F)?;
        self.write_reg(i2c, CLK_MANAGER2, 0x00)?;

        // System: DAC-only mode
        self.write_reg(i2c, SYS_MODSEL, 0x08)?;

        // DAC settings
        self.write_reg(i2c, DAC_MUTE, 0x00)?; // Unmute
        self.set_volume_internal(i2c, self.volume)?;

        Ok(())
    }

    /// Set DAC volume (0-100%)
    #[cfg(not(feature = "esp32s3"))]
    pub fn set_volume(&mut self, volume: u8) -> Result<(), Es8311Error> {
        self.volume = volume.min(100);
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn set_volume(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        volume: u8,
    ) -> Result<(), Es8311Error> {
        self.volume = volume.min(100);
        self.set_volume_internal(i2c, self.volume)
    }

    #[cfg(feature = "esp32s3")]
    fn set_volume_internal(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        volume: u8,
    ) -> Result<(), Es8311Error> {
        // ES8311 DAC volume: 0x00 = 0dB, 0xBF = -95.5dB
        // Map 0-100% to register value
        let reg_val = if volume >= 100 {
            0x00
        } else {
            ((100 - volume) as u16 * 0xBF / 100) as u8
        };

        self.write_reg(i2c, regs::DAC_VOL, reg_val)
    }

    /// Mute DAC output
    #[cfg(not(feature = "esp32s3"))]
    pub fn mute(&mut self, mute: bool) -> Result<(), Es8311Error> {
        self.muted = mute;
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn mute(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        mute: bool,
    ) -> Result<(), Es8311Error> {
        self.muted = mute;
        let val = if mute { 0x08 } else { 0x00 };
        self.write_reg(i2c, regs::DAC_MUTE, val)
    }

    /// Get current volume
    pub fn volume(&self) -> u8 {
        self.volume
    }

    /// Check if muted
    pub fn is_muted(&self) -> bool {
        self.muted
    }

    #[cfg(feature = "esp32s3")]
    fn write_reg(
        &self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        reg: u8,
        val: u8,
    ) -> Result<(), Es8311Error> {
        i2c.write(ES8311_ADDR, &[reg, val])
            .map_err(|_| Es8311Error::I2cError)
    }

    #[cfg(feature = "esp32s3")]
    fn read_reg(
        &self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        reg: u8,
    ) -> Result<u8, Es8311Error> {
        let mut buf = [0u8; 1];
        i2c.write_read(ES8311_ADDR, &[reg], &mut buf)
            .map_err(|_| Es8311Error::I2cError)?;
        Ok(buf[0])
    }
}
