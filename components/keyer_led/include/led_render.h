/**
 * @file led_render.h
 * @brief What the seven LEDs show, as a pure function
 *
 * The rule the whole vocabulary obeys, and the only thing an operator has
 * to remember with a paddle in one hand:
 *
 *     Green means your CW goes out. Any other colour means it does not,
 *     and the colour says why.
 *
 * So colour carries the situation and nothing else. The paddle overlay
 * carries position and brightness only (#26): a green overlay on a red
 * base would contradict the rule at the exact moment the operator is
 * keying, which is when the rule has to hold.
 *
 * This header has no ESP-IDF dependency: the render is a pure function of
 * the situation, the clocks and the paddle, so it is proven by host tests
 * rather than by looking at the strip. led.c is the RMT shell around it.
 */

#ifndef KEYER_LED_RENDER_H
#define KEYER_LED_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest strip the render will fill */
#define LED_MAX_COUNT 16

/* Colours, 0xRRGGBB. One per situation, and no others: a colour that
 * belongs to nothing cannot be misread. */
#define LED_COLOR_OFF     0x000000U
#define LED_COLOR_RED     0xFF0000U  /**< off air */
#define LED_COLOR_GREEN   0x00FF00U  /**< on air, and nothing else is ever green */
#define LED_COLOR_BLUE    0x0000FFU  /**< setup */
#define LED_COLOR_ORANGE  0xFF8000U  /**< starting */
#define LED_COLOR_YELLOW  0xFFA000U  /**< local only */

/* Animation clocks */
#define LED_BREATH_PERIOD_US   2000000  /**< full breath, rise and fall */
#define LED_FLASH_DURATION_US   100000  /**< one flash of a notification */
#define LED_FLASH_GAP_US        100000  /**< the dark between two flashes */
#define LED_NOTIFY_FLASHES            3
#define LED_NOTIFY_TOTAL_US  (LED_NOTIFY_FLASHES * (LED_FLASH_DURATION_US + LED_FLASH_GAP_US))

/**
 * @brief What the box is doing, in the operator's terms
 *
 * Five situations, one colour each. They are situations and not states:
 * nothing here is a transition, an animation or a notification, because
 * those are events and live on their own clock (led_notify()).
 */
typedef enum {
    LED_SITUATION_STARTING = 0, /**< orange, breathing: not usable yet */
    LED_SITUATION_SETUP,        /**< blue, breathing: come to the access point */
    LED_SITUATION_LOCAL_ONLY,   /**< yellow, dim: the network is gone, the keyer is local */
    LED_SITUATION_ON_AIR,       /**< green, dim: your keying goes out */
    LED_SITUATION_OFF_AIR,      /**< red, dim: connected, and your keying is dropped */
} led_situation_t;

/** Everything the render needs, and nothing it could read for itself */
typedef struct {
    led_situation_t situation;
    int64_t situation_elapsed_us; /**< since the situation was entered */
    int64_t notify_elapsed_us;    /**< since led_notify(); negative when none is running */
    bool    dit;                  /**< the DIT pin, read raw: this is a hardware indication */
    bool    dah;                  /**< the DAH pin, read raw */
    uint8_t count;                /**< LEDs on the strip, clamped to LED_MAX_COUNT */
    uint8_t brightness;           /**< 0-100, the lit level */
    uint8_t brightness_dim;       /**< 0-100, the resting level of a steady situation */
} led_scene_t;

/** One colour per LED, 0xRRGGBB */
typedef struct {
    uint32_t color[LED_MAX_COUNT];
    uint8_t  count;
} led_frame_t;

/**
 * @brief The colour that belongs to a situation
 *
 * @return 0xRRGGBB; LED_COLOR_GREEN only for LED_SITUATION_ON_AIR
 */
uint32_t led_situation_color(led_situation_t situation);

/** @return the triangle-wave level of a breathing situation, 0..brightness */
uint8_t led_breathing_level(int64_t elapsed_us, uint8_t brightness);

/**
 * @brief Fill one frame from one scene
 *
 * Base colour from the situation, at the breathing or the resting level; a
 * running notification overrides that level with its flashes, in the same
 * colour it is landing on; the paddle overlay then lights its LEDs at full
 * brightness in that same colour. No step introduces a colour of its own.
 *
 * Does nothing when either argument is NULL.
 */
void led_render(const led_scene_t *scene, led_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_LED_RENDER_H */
