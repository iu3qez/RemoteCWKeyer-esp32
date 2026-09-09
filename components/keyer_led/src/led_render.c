/**
 * @file led_render.c
 * @brief The pure half of the LED driver: a scene becomes a frame
 */

#include "led_render.h"

/** Scale a 0xRRGGBB colour by a percentage */
static uint32_t scale(uint32_t color, uint8_t brightness)
{
    if (brightness >= 100U) {
        return color;
    }
    uint32_t r = ((color >> 16) & 0xFFU) * brightness / 100U;
    uint32_t g = ((color >> 8) & 0xFFU) * brightness / 100U;
    uint32_t b = (color & 0xFFU) * brightness / 100U;
    return (r << 16) | (g << 8) | b;
}

uint32_t led_situation_color(led_situation_t situation)
{
    switch (situation) {
        case LED_SITUATION_STARTING:   return LED_COLOR_ORANGE;
        case LED_SITUATION_SETUP:      return LED_COLOR_BLUE;
        case LED_SITUATION_LOCAL_ONLY: return LED_COLOR_YELLOW;
        case LED_SITUATION_ON_AIR:     return LED_COLOR_GREEN;
        case LED_SITUATION_OFF_AIR:    return LED_COLOR_RED;
        default:                       return LED_COLOR_OFF;
    }
}

uint8_t led_breathing_level(int64_t elapsed_us, uint8_t brightness)
{
    int64_t phase = elapsed_us % LED_BREATH_PERIOD_US;
    if (phase < 0) {
        phase += LED_BREATH_PERIOD_US;
    }
    const int64_t half = LED_BREATH_PERIOD_US / 2;
    int64_t rising = (phase < half) ? phase : (LED_BREATH_PERIOD_US - phase);
    return (uint8_t)((rising * (int64_t)brightness) / half);
}

void led_render(const led_scene_t *scene, led_frame_t *out)
{
    if (scene == NULL || out == NULL) {
        return;
    }

    uint8_t count = (scene->count > LED_MAX_COUNT) ? (uint8_t)LED_MAX_COUNT : scene->count;
    out->count = count;

    const uint32_t hue = led_situation_color(scene->situation);
    const bool breathing = (scene->situation == LED_SITUATION_STARTING) ||
                           (scene->situation == LED_SITUATION_SETUP);

    uint8_t level = breathing
                  ? led_breathing_level(scene->situation_elapsed_us, scene->brightness)
                  : scene->brightness_dim;

    /* A notification is an event asking for attention, so it flashes in the
     * colour it is landing on rather than a colour of its own. */
    if (scene->notify_elapsed_us >= 0 && scene->notify_elapsed_us < LED_NOTIFY_TOTAL_US) {
        int64_t phase = scene->notify_elapsed_us % (LED_FLASH_DURATION_US + LED_FLASH_GAP_US);
        level = (phase < LED_FLASH_DURATION_US) ? scene->brightness : 0U;
    }

    const uint32_t base = scale(hue, level);
    for (uint8_t i = 0; i < count; i++) {
        out->color[i] = base;
    }

    if (!scene->dit && !scene->dah) {
        return;
    }

    /* The overlay is the raw pins, so it says whether the contact closes at
     * all: lit but silent is a software fault, dark is the contact or the
     * wiring. It carries position and brightness, never a colour. */
    const uint32_t lit = scale(hue, scene->brightness);
    const uint8_t center = (uint8_t)(count / 2U);

    if (scene->dit && scene->dah) {
        for (uint8_t i = 0; i < count; i++) {
            out->color[i] = lit;
        }
    } else if (scene->dit) {
        for (uint8_t i = 0; i < center; i++) {
            out->color[i] = lit;
        }
    } else {
        for (uint8_t i = (uint8_t)(center + 1U); i < count; i++) {
            out->color[i] = lit;
        }
    }
}
