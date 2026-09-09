/**
 * @file test_led.c
 * @brief The LED vocabulary, pinned to its one rule
 *
 * The rule (#26, led_render.h): green means the operator's CW goes out,
 * and every other colour means it does not. The render is a pure function
 * precisely so that rule can be proven here instead of looked at on a
 * bench, and the test that matters is the one that would fail the moment
 * some future state, overlay or notification lights green for anything
 * but being on air.
 */

#include "unity.h"
#include "led_render.h"
#include <string.h>

/* Exact arithmetic: at 100 a colour is itself, at 10 each channel is a
 * tenth, so the expectations below are written out rather than recomputed
 * with the implementation's own formula. */
#define FULL 100
#define DIM   10

static const led_situation_t all_situations[] = {
    LED_SITUATION_STARTING,
    LED_SITUATION_SETUP,
    LED_SITUATION_LOCAL_ONLY,
    LED_SITUATION_ON_AIR,
    LED_SITUATION_OFF_AIR,
};

static led_scene_t scene_of(led_situation_t s) {
    led_scene_t scene;
    memset(&scene, 0, sizeof(scene));
    scene.situation = s;
    scene.notify_elapsed_us = -1;
    scene.count = 7;
    scene.brightness = FULL;
    scene.brightness_dim = DIM;
    return scene;
}

/* Green on the wire: no red, some green, no blue. Orange and yellow both
 * carry red, blue carries only blue, so this catches exactly the lie. */
static bool is_green(uint32_t c) {
    return ((c >> 16) & 0xFFU) == 0U && ((c >> 8) & 0xFFU) > 0U && (c & 0xFFU) == 0U;
}

void test_led_green_belongs_to_on_air_and_to_nothing_else(void) {
    /* Every situation, across a full breath, with every paddle combination:
     * the only frames carrying green are the on-air ones. */
    static const int64_t phases[] = {0, 250000, 500000, 999999, 1000000, 1500000, 1999999};

    for (size_t s = 0; s < sizeof(all_situations) / sizeof(all_situations[0]); s++) {
        for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++) {
            for (int paddles = 0; paddles < 4; paddles++) {
                led_scene_t scene = scene_of(all_situations[s]);
                scene.situation_elapsed_us = phases[p];
                scene.dit = (paddles & 1) != 0;
                scene.dah = (paddles & 2) != 0;

                led_frame_t frame;
                memset(&frame, 0, sizeof(frame));
                led_render(&scene, &frame);

                bool on_air = (all_situations[s] == LED_SITUATION_ON_AIR);
                for (uint8_t i = 0; i < frame.count; i++) {
                    if (!on_air) {
                        TEST_ASSERT_FALSE_MESSAGE(is_green(frame.color[i]),
                            "a situation that is not on air lit an LED green");
                    }
                }
            }
        }
    }
}

void test_led_each_situation_carries_its_own_colour(void) {
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_ORANGE, led_situation_color(LED_SITUATION_STARTING));
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_BLUE,   led_situation_color(LED_SITUATION_SETUP));
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_YELLOW, led_situation_color(LED_SITUATION_LOCAL_ONLY));
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_GREEN,  led_situation_color(LED_SITUATION_ON_AIR));
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_RED,    led_situation_color(LED_SITUATION_OFF_AIR));

    /* Five situations, five distinct colours: none is a synonym of another */
    for (size_t a = 0; a < 5; a++) {
        for (size_t b = a + 1; b < 5; b++) {
            TEST_ASSERT_NOT_EQUAL(led_situation_color(all_situations[a]),
                                  led_situation_color(all_situations[b]));
        }
    }
}

void test_led_a_steady_situation_lights_the_whole_strip_dim(void) {
    led_scene_t scene = scene_of(LED_SITUATION_OFF_AIR);
    led_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    led_render(&scene, &frame);

    TEST_ASSERT_EQUAL_UINT8(7, frame.count);
    for (uint8_t i = 0; i < frame.count; i++) {
        TEST_ASSERT_EQUAL_HEX32(0x190000U, frame.color[i]);  /* red at a tenth */
    }
}

/*
 * The overlay is the raw DIT/DAH pins, so it answers "does the contact
 * close at all" without the debounce or the FSM in the way. It carries
 * position and brightness and never a colour, which is what keeps the
 * rule true while the operator is actually keying: here the situation is
 * off air, and the lit LEDs must be red, not the green they used to be.
 */
void test_led_overlay_carries_position_and_never_a_colour(void) {
    led_frame_t frame;

    led_scene_t dit = scene_of(LED_SITUATION_OFF_AIR);
    dit.dit = true;
    memset(&frame, 0, sizeof(frame));
    led_render(&dit, &frame);
    for (uint8_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_HEX32(0xFF0000U, frame.color[i]);   /* left three, full red */
    }
    for (uint8_t i = 3; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX32(0x190000U, frame.color[i]);   /* the rest at rest */
    }

    led_scene_t dah = scene_of(LED_SITUATION_OFF_AIR);
    dah.dah = true;
    memset(&frame, 0, sizeof(frame));
    led_render(&dah, &frame);
    for (uint8_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_HEX32(0x190000U, frame.color[i]);
    }
    for (uint8_t i = 4; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX32(0xFF0000U, frame.color[i]);   /* right three */
    }

    /* Squeeze is both sides and the centre, so the whole strip lights: one
     * more picture in the same colour, where the magenta pixel used to be. */
    led_scene_t squeeze = scene_of(LED_SITUATION_OFF_AIR);
    squeeze.dit = true;
    squeeze.dah = true;
    memset(&frame, 0, sizeof(frame));
    led_render(&squeeze, &frame);
    for (uint8_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX32(0xFF0000U, frame.color[i]);
    }
}

void test_led_overlay_shows_the_paddle_in_every_situation(void) {
    /* It used to exist only in the idle state, so a box that was not idle
     * showed nothing about the paddle at all: exactly when a diagnosis is
     * wanted. Every situation now carries it. */
    for (size_t s = 0; s < sizeof(all_situations) / sizeof(all_situations[0]); s++) {
        led_scene_t resting = scene_of(all_situations[s]);
        led_scene_t keyed = scene_of(all_situations[s]);
        keyed.dit = true;

        led_frame_t a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        led_render(&resting, &a);
        led_render(&keyed, &b);

        TEST_ASSERT_NOT_EQUAL_MESSAGE(a.color[0], b.color[0],
            "closing DIT changed nothing on the strip in some situation");
    }
}

void test_led_notification_flashes_in_the_colour_it_lands_on(void) {
    led_frame_t frame;

    /* Landing on off air: the attention flashes are red, not a colour of
     * their own, so an event can never announce something the situation
     * contradicts. */
    led_scene_t lit = scene_of(LED_SITUATION_OFF_AIR);
    lit.notify_elapsed_us = 0;
    memset(&frame, 0, sizeof(frame));
    led_render(&lit, &frame);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000U, frame.color[0]);

    led_scene_t gap = scene_of(LED_SITUATION_OFF_AIR);
    gap.notify_elapsed_us = LED_FLASH_DURATION_US;
    memset(&frame, 0, sizeof(frame));
    led_render(&gap, &frame);
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_OFF, frame.color[0]);

    /* Three flashes, then the situation has the strip back */
    led_scene_t done = scene_of(LED_SITUATION_OFF_AIR);
    done.notify_elapsed_us = LED_NOTIFY_TOTAL_US;
    memset(&frame, 0, sizeof(frame));
    led_render(&done, &frame);
    TEST_ASSERT_EQUAL_HEX32(0x190000U, frame.color[0]);

    /* A paddle still wins over a flash: the pin is the more urgent fact */
    led_scene_t keyed = scene_of(LED_SITUATION_OFF_AIR);
    keyed.notify_elapsed_us = LED_FLASH_DURATION_US;  /* dark phase */
    keyed.dit = true;
    memset(&frame, 0, sizeof(frame));
    led_render(&keyed, &frame);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000U, frame.color[0]);
}

void test_led_breathing_rises_and_falls_over_the_period(void) {
    TEST_ASSERT_EQUAL_UINT8(0,   led_breathing_level(0, FULL));
    TEST_ASSERT_EQUAL_UINT8(50,  led_breathing_level(LED_BREATH_PERIOD_US / 4, FULL));
    TEST_ASSERT_EQUAL_UINT8(100, led_breathing_level(LED_BREATH_PERIOD_US / 2, FULL));
    TEST_ASSERT_EQUAL_UINT8(50,  led_breathing_level(3 * (LED_BREATH_PERIOD_US / 4), FULL));
    /* It cycles: the next period repeats the first */
    TEST_ASSERT_EQUAL_UINT8(100, led_breathing_level(LED_BREATH_PERIOD_US + LED_BREATH_PERIOD_US / 2, FULL));

    /* Only the two breathing situations breathe; the others sit still */
    led_scene_t starting_low = scene_of(LED_SITUATION_STARTING);
    led_scene_t starting_high = scene_of(LED_SITUATION_STARTING);
    starting_high.situation_elapsed_us = LED_BREATH_PERIOD_US / 2;
    led_frame_t low, high;
    memset(&low, 0, sizeof(low));
    memset(&high, 0, sizeof(high));
    led_render(&starting_low, &low);
    led_render(&starting_high, &high);
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_OFF, low.color[0]);
    TEST_ASSERT_EQUAL_HEX32(LED_COLOR_ORANGE, high.color[0]);

    led_scene_t steady_a = scene_of(LED_SITUATION_ON_AIR);
    led_scene_t steady_b = scene_of(LED_SITUATION_ON_AIR);
    steady_b.situation_elapsed_us = LED_BREATH_PERIOD_US / 2;
    memset(&low, 0, sizeof(low));
    memset(&high, 0, sizeof(high));
    led_render(&steady_a, &low);
    led_render(&steady_b, &high);
    TEST_ASSERT_EQUAL_HEX32(low.color[0], high.color[0]);
}

void test_led_render_clamps_the_strip_and_survives_null(void) {
    led_scene_t scene = scene_of(LED_SITUATION_ON_AIR);
    scene.count = LED_MAX_COUNT + 5;
    led_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    led_render(&scene, &frame);
    TEST_ASSERT_EQUAL_UINT8(LED_MAX_COUNT, frame.count);

    led_render(NULL, &frame);
    led_render(&scene, NULL);
    TEST_ASSERT_EQUAL_UINT8(LED_MAX_COUNT, frame.count);
}
