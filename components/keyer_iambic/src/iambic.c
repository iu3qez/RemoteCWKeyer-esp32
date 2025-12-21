/**
 * @file iambic.c
 * @brief Iambic keyer FSM implementation
 *
 * Pure logic, no I/O or allocation. Fully testable on host.
 */

#include "iambic.h"
#include <assert.h>
#include <string.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void update_gpio(iambic_processor_t *proc, gpio_state_t gpio);
static void tick_idle(iambic_processor_t *proc, int64_t now_us);
static void tick_sending(iambic_processor_t *proc, int64_t now_us, iambic_element_t element);
static void tick_gap(iambic_processor_t *proc, int64_t now_us);
static iambic_element_t *decide_next_element(iambic_processor_t *proc, iambic_element_t *out);
static void start_element(iambic_processor_t *proc, iambic_element_t element, int64_t now_us);

/* ============================================================================
 * Public Functions
 * ============================================================================ */

void iambic_init(iambic_processor_t *proc, const iambic_config_t *config) {
    assert(proc != NULL);
    assert(config != NULL);

    proc->config = *config;
    proc->state = IAMBIC_STATE_IDLE;
    proc->element_end_us = 0;
    proc->last_element = ELEMENT_DAH;  /* Start with DAH so first DIT press works */
    proc->dit_pressed = false;
    proc->dah_pressed = false;
    proc->dit_memory = false;
    proc->dah_memory = false;
    proc->squeeze_seen = false;
    proc->key_down = false;
}

void iambic_set_config(iambic_processor_t *proc, const iambic_config_t *config) {
    assert(proc != NULL);
    assert(config != NULL);

    proc->config = *config;
}

stream_sample_t iambic_tick(iambic_processor_t *proc, int64_t now_us, gpio_state_t gpio) {
    assert(proc != NULL);

    /* Update paddle state */
    update_gpio(proc, gpio);

    /* Run FSM */
    switch (proc->state) {
        case IAMBIC_STATE_IDLE:
            tick_idle(proc, now_us);
            break;
        case IAMBIC_STATE_SEND_DIT:
            tick_sending(proc, now_us, ELEMENT_DIT);
            break;
        case IAMBIC_STATE_SEND_DAH:
            tick_sending(proc, now_us, ELEMENT_DAH);
            break;
        case IAMBIC_STATE_GAP:
            tick_gap(proc, now_us);
            break;
    }

    /* Produce output sample */
    stream_sample_t sample = STREAM_SAMPLE_EMPTY;
    sample.gpio = gpio;
    sample.local_key = proc->key_down ? 1 : 0;
    /* audio_level filled by audio envelope later */
    /* flags and config_gen set by RT loop */

    return sample;
}

void iambic_reset(iambic_processor_t *proc) {
    assert(proc != NULL);

    proc->state = IAMBIC_STATE_IDLE;
    proc->element_end_us = 0;
    proc->dit_memory = false;
    proc->dah_memory = false;
    proc->squeeze_seen = false;
    proc->key_down = false;
}

/* ============================================================================
 * Private Functions
 * ============================================================================ */

static void update_gpio(iambic_processor_t *proc, gpio_state_t gpio) {
    bool was_squeeze = proc->dit_pressed && proc->dah_pressed;

    proc->dit_pressed = gpio_dit(gpio);
    proc->dah_pressed = gpio_dah(gpio);

    bool is_squeeze = proc->dit_pressed && proc->dah_pressed;

    /* Track squeeze for Mode B */
    if (is_squeeze && !was_squeeze) {
        proc->squeeze_seen = true;
    }

    /* Arm memory when paddle pressed during element/gap */
    if (proc->state != IAMBIC_STATE_IDLE) {
        if (proc->dit_pressed && proc->config.dit_memory) {
            proc->dit_memory = true;
        }
        if (proc->dah_pressed && proc->config.dah_memory) {
            proc->dah_memory = true;
        }
    }
}

static void tick_idle(iambic_processor_t *proc, int64_t now_us) {
    /* Determine next element from memory or current paddle state */
    iambic_element_t next_element;
    iambic_element_t *next = decide_next_element(proc, &next_element);

    if (next != NULL) {
        start_element(proc, *next, now_us);
    }
}

static void tick_sending(iambic_processor_t *proc, int64_t now_us, iambic_element_t element) {
    if (now_us >= proc->element_end_us) {
        /* Element complete */
        proc->key_down = false;
        proc->last_element = element;

        /* Enter gap */
        proc->state = IAMBIC_STATE_GAP;
        proc->element_end_us = now_us + iambic_gap_duration_us(&proc->config);
    }
}

static void tick_gap(iambic_processor_t *proc, int64_t now_us) {
    if (now_us >= proc->element_end_us) {
        /* Gap complete */
        proc->state = IAMBIC_STATE_IDLE;

        /* Immediately check for next element */
        tick_idle(proc, now_us);
    }
}

static iambic_element_t *decide_next_element(iambic_processor_t *proc, iambic_element_t *out) {
    /* Priority 1: Memory (armed during previous element) */
    if (proc->dit_memory) {
        proc->dit_memory = false;
        *out = ELEMENT_DIT;
        return out;
    }
    if (proc->dah_memory) {
        proc->dah_memory = false;
        *out = ELEMENT_DAH;
        return out;
    }

    /* Priority 2: Mode B bonus element */
    if (proc->config.mode == IAMBIC_MODE_B && proc->squeeze_seen) {
        /* Check if squeeze was released */
        if (!(proc->dit_pressed && proc->dah_pressed)) {
            proc->squeeze_seen = false;
            *out = iambic_element_opposite(proc->last_element);
            return out;
        }
    }

    /* Priority 3: Current paddle state */
    if (proc->dit_pressed && proc->dah_pressed) {
        /* Squeeze: alternate from last element */
        *out = iambic_element_opposite(proc->last_element);
        return out;
    } else if (proc->dit_pressed) {
        *out = ELEMENT_DIT;
        return out;
    } else if (proc->dah_pressed) {
        *out = ELEMENT_DAH;
        return out;
    } else {
        /* No paddle pressed */
        proc->squeeze_seen = false;
        return NULL;
    }
}

static void start_element(iambic_processor_t *proc, iambic_element_t element, int64_t now_us) {
    proc->key_down = true;
    proc->squeeze_seen = proc->dit_pressed && proc->dah_pressed;

    int64_t duration;
    switch (element) {
        case ELEMENT_DIT:
            duration = iambic_dit_duration_us(&proc->config);
            proc->state = IAMBIC_STATE_SEND_DIT;
            break;
        case ELEMENT_DAH:
            duration = iambic_dah_duration_us(&proc->config);
            proc->state = IAMBIC_STATE_SEND_DAH;
            break;
        default:
            /* Should not happen */
            duration = 0;
            break;
    }

    proc->element_end_us = now_us + duration;
}
