# keyer_hal - Hardware Abstraction Layer
#
# GPIO for paddle input and TX output.
# I2S for audio output.
# I2C for ES8311 codec control.

idf_component_register(
    SRCS
        "src/hal_gpio.c"
        "src/hal_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core driver esp_driver_gpio esp_driver_i2s esp_driver_i2c
    PRIV_REQUIRES esp_codec_dev esp_io_expander esp_io_expander_tca95xx_16bit
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
)
