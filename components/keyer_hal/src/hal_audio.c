/**
 * @file hal_audio.c
 * @brief I2S audio HAL implementation (stub)
 */

#include "hal_audio.h"

#ifdef CONFIG_IDF_TARGET
#include "driver/i2s_std.h"

/* TODO: Implement I2S audio output */

void hal_audio_init(const hal_audio_config_t *config) {
    (void)config;
    /* TODO */
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    (void)samples;
    (void)count;
    return count;
}

void hal_audio_start(void) {
    /* TODO */
}

void hal_audio_stop(void) {
    /* TODO */
}

#else
/* Host stub */

void hal_audio_init(const hal_audio_config_t *config) {
    (void)config;
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    (void)samples;
    return count;
}

void hal_audio_start(void) {}
void hal_audio_stop(void) {}

#endif /* CONFIG_IDF_TARGET */
