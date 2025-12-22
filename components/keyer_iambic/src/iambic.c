/**
 * @file iambic.c
 * @brief Iambic keyer FSM implementation
 *
 * Pure logic, no I/O or allocation. Fully testable on host.
 *
 * Memory Window Logic:
 * Paddle inputs are only memorized when within the memory window
 * (between mem_window_start_pct and mem_window_end_pct of element duration).
 */

#include "iambic.h"
#include <assert.h>
#include <string.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void update_gpio(iambic_processor_t *proc, gpio_state_t gpio, int64_t now_us);
static void tick_idle(iambic_processor_t *proc, int64_t now_us);
static void tick_sending(iambic_processor_t *proc, int64_t now_us, iambic_element_t element);
static void tick_gap(iambic_processor_t *proc, int64_t now_us);
static iambic_element_t *decide_next_element(iambic_processor_t *proc, iambic_element_t *out);
static void start_element(iambic_processor_t *proc, iambic_element_t element, int64_t now_us);
static bool is_in_memory_window(const iambic_processor_t *proc, int64_t now_us);

/* ============================================================================
 * Public Functions
 * ============================================================================ */

void iambic_init(iambic_processor_t *proc, const iambic_config_t *config) {
    assert(proc != NULL);
    assert(config != NULL);

    proc->config = *config;
    proc->state = IAMBIC_STATE_IDLE;
    proc->element_start_us = 0;
    proc->element_end_us = 0;
    proc->element_duration_us = 0;
    proc->last_element = ELEMENT_DAH;  /* Start with DAH so first DIT press works */
    proc->dit_pressed = false;
    proc->dah_pressed = false;
    proc->dit_memory = false;
    proc->dah_memory = false;
    proc->squeeze_seen = false;
    proc->squeeze_latched = false;
    proc->key_down = false;
}

void iambic_set_config(iambic_processor_t *proc, const iambic_config_t *config) {
    assert(proc != NULL);
    assert(config != NULL);

    proc->config = *config;
}

stream_sample_t iambic_tick(iambic_processor_t *proc, int64_t now_us, gpio_state_t gpio) {
    assert(proc != NULL);

    /* Update paddle state and memory */
    update_gpio(proc, gpio, now_us);

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
    proc->element_start_us = 0;
    proc->element_end_us = 0;
    proc->element_duration_us = 0;
    proc->dit_memory = false;
    proc->dah_memory = false;
    proc->squeeze_seen = false;
    proc->squeeze_latched = false;
    proc->key_down = false;
}

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * @brief Check if current time is within the memory window of current element
 */
static bool is_in_memory_window(const iambic_processor_t *proc, int64_t now_us) {
    /* Only applies during element transmission (not gap) */
    if (proc->state != IAMBIC_STATE_SEND_DIT && proc->state != IAMBIC_STATE_SEND_DAH) {
        return true;  /* Always accept during gap */
    }

    if (proc->element_duration_us <= 0) {
        return true;  /* Fallback: no window defined */
    }

    /* Calculate progress percentage (0-100) */
    int64_t elapsed = now_us - proc->element_start_us;
    if (elapsed < 0) {
        elapsed = 0;
    }

    /* Avoid overflow: (elapsed * 100) could overflow for large durations */
    uint8_t progress_pct;
    if (elapsed >= proc->element_duration_us) {
        progress_pct = 100;
    } else {
        progress_pct = (uint8_t)((elapsed * 100) / proc->element_duration_us);
    }

    return iambic_in_memory_window(progress_pct,
                                    proc->config.mem_window_start_pct,
                                    proc->config.mem_window_end_pct);
}

static void update_gpio(iambic_processor_t *proc, gpio_state_t gpio, int64_t now_us) {
    bool was_squeeze = proc->dit_pressed && proc->dah_pressed;

    proc->dit_pressed = gpio_dit(gpio);
    proc->dah_pressed = gpio_dah(gpio);

    bool is_squeeze = proc->dit_pressed && proc->dah_pressed;

    /* Track squeeze for Mode B */
    if (is_squeeze && !was_squeeze) {
        proc->squeeze_seen = true;
    }

    /* Handle squeeze mode (LATCH_ON vs LATCH_OFF) */
    if (proc->config.squeeze_mode == SQUEEZE_MODE_LATCH_ON) {
        /* LATCH_ON: Capture squeeze state at element start, don't update during element */
        /* squeeze_latched is set in start_element() and used for squeeze detection */
    } else {
        /* LATCH_OFF: Live mode - continuously update squeeze_latched */
        proc->squeeze_latched = is_squeeze;
    }

    /* Arm memory when paddle pressed during element/gap AND within memory window */
    if (proc->state != IAMBIC_STATE_IDLE) {
        bool in_window = is_in_memory_window(proc, now_us);

        if (in_window) {
            /*
             * Only arm memory for the OPPOSITE element to what's currently being sent.
             * This prevents re-arming the same element during a squeeze.
             * During GAP, both can be armed since no element is active.
             */
            bool can_arm_dit = (proc->state != IAMBIC_STATE_SEND_DIT);
            bool can_arm_dah = (proc->state != IAMBIC_STATE_SEND_DAH);

            if (can_arm_dit && proc->dit_pressed && iambic_dit_memory_enabled(proc->config.memory_mode)) {
                proc->dit_memory = true;
            }
            if (can_arm_dah && proc->dah_pressed && iambic_dah_memory_enabled(proc->config.memory_mode)) {
                proc->dah_memory = true;
            }
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
        proc->element_start_us = now_us;
        proc->element_duration_us = iambic_gap_duration_us(&proc->config);
        proc->element_end_us = now_us + proc->element_duration_us;
    }
}

static void tick_gap(iambic_processor_t *proc, int64_t now_us) {
    if (now_us >= proc->element_end_us) {
        /* Gap complete */
        proc->state = IAMBIC_STATE_IDLE;
        proc->element_duration_us = 0;

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
        bool current_squeeze = (proc->config.squeeze_mode == SQUEEZE_MODE_LATCH_ON)
                                ? proc->squeeze_latched
                                : (proc->dit_pressed && proc->dah_pressed);
        if (!current_squeeze) {
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

    /* Latch squeeze state at element start (for LATCH_ON mode) */
    proc->squeeze_latched = proc->dit_pressed && proc->dah_pressed;
    proc->squeeze_seen = proc->squeeze_latched;

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

    proc->element_start_us = now_us;
    proc->element_duration_us = duration;
    proc->element_end_us = now_us + duration;
}
