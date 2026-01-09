/**
 * @file text_keyer.c
 * @brief Text-to-Morse keyer implementation
 */

#include "text_keyer.h"
#include "morse_table.h"
#include "config.h"
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Internal Types
 * ============================================================================ */

typedef enum {
    ELEMENT_DIT,
    ELEMENT_DAH,
    ELEMENT_INTRA_GAP,
    ELEMENT_CHAR_GAP,
    ELEMENT_WORD_GAP,
} element_type_t;

typedef struct {
    char text[TEXT_KEYER_MAX_LEN];
    size_t text_len;
    size_t char_index;
    const char *current_pattern;
    size_t pattern_index;
    element_type_t element;
    int64_t element_end_us;
    bool key_down;
} send_state_t;

/* ============================================================================
 * Module State
 * ============================================================================ */

static const atomic_bool *s_paddle_abort = NULL;
static text_keyer_state_t s_state = TEXT_KEYER_IDLE;
static send_state_t s_send = {0};

/* Atomic key state for RT task polling (Core 0 reads, Core 1 writes) */
static atomic_bool s_key_down = ATOMIC_VAR_INIT(false);

/* ============================================================================
 * Timing Helpers
 * ============================================================================ */

static int64_t dit_duration_us(void) {
    uint32_t wpm = CONFIG_GET_WPM();
    if (wpm < 5) wpm = 5;
    if (wpm > 60) wpm = 60;
    return 1200000 / (int64_t)wpm;
}

/* ============================================================================
 * Key State Management
 * ============================================================================ */

static void set_key_down(bool key_down) {
    atomic_store_explicit(&s_key_down, key_down, memory_order_release);
}

/* ============================================================================
 * Pattern Navigation
 * ============================================================================ */

static const char *get_next_pattern(void) {
    while (s_send.char_index < s_send.text_len) {
        char c = s_send.text[s_send.char_index];

        /* Check for prosign */
        if (c == '<') {
            const char *pattern = NULL;
            size_t len = morse_match_prosign(&s_send.text[s_send.char_index], &pattern);
            if (len > 0 && pattern != NULL) {
                s_send.char_index += len;
                return pattern;
            }
        }

        /* Space = word gap */
        if (c == ' ') {
            s_send.char_index++;
            return " ";  /* Special marker for word gap */
        }

        /* Regular character */
        const char *pattern = morse_table_reverse(c);
        s_send.char_index++;

        if (pattern != NULL) {
            return pattern;
        }
        /* Skip unknown characters */
    }
    return NULL;
}

static bool start_next_element(int64_t now_us) {
    int64_t dit_us = dit_duration_us();

    /* Need new pattern? */
    if (s_send.current_pattern == NULL ||
        s_send.current_pattern[s_send.pattern_index] == '\0') {

        /* Char gap before next character (unless first or word gap) */
        if (s_send.current_pattern != NULL &&
            s_send.current_pattern[0] != ' ') {
            s_send.element = ELEMENT_CHAR_GAP;
            s_send.element_end_us = now_us + (dit_us * 3);
            s_send.key_down = false;
            set_key_down(false);
        }

        s_send.current_pattern = get_next_pattern();
        s_send.pattern_index = 0;

        if (s_send.current_pattern == NULL) {
            return false;  /* Done */
        }

        /* Word gap */
        if (s_send.current_pattern[0] == ' ') {
            s_send.element = ELEMENT_WORD_GAP;
            s_send.element_end_us = now_us + (dit_us * 7);
            s_send.key_down = false;
            set_key_down(false);
            s_send.current_pattern = NULL;  /* Force get next pattern */
            return true;
        }

        /* Skip char gap if we just set one */
        if (s_send.element == ELEMENT_CHAR_GAP) {
            return true;
        }
    }

    /* Send dit or dah */
    char elem = s_send.current_pattern[s_send.pattern_index++];

    if (elem == '.') {
        s_send.element = ELEMENT_DIT;
        s_send.element_end_us = now_us + dit_us;
    } else if (elem == '-') {
        s_send.element = ELEMENT_DAH;
        s_send.element_end_us = now_us + (dit_us * 3);
    } else {
        return start_next_element(now_us);  /* Skip invalid */
    }

    s_send.key_down = true;
    set_key_down(true);
    return true;
}

static void finish_element(int64_t now_us) {
    int64_t dit_us = dit_duration_us();

    if (s_send.key_down) {
        s_send.key_down = false;
        set_key_down(false);

        /* Intra-element gap if more elements in pattern */
        if (s_send.current_pattern != NULL &&
            s_send.current_pattern[s_send.pattern_index] != '\0') {
            s_send.element = ELEMENT_INTRA_GAP;
            s_send.element_end_us = now_us + dit_us;
            return;
        }
    }

    s_send.element_end_us = 0;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int text_keyer_init(const text_keyer_config_t *config) {
    if (config == NULL) {
        return -1;
    }

    s_paddle_abort = config->paddle_abort;
    s_state = TEXT_KEYER_IDLE;
    set_key_down(false);
    memset(&s_send, 0, sizeof(s_send));

    return 0;
}

int text_keyer_send(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return -1;
    }

    if (s_state != TEXT_KEYER_IDLE) {
        return -1;
    }

    size_t len = strlen(text);
    if (len >= TEXT_KEYER_MAX_LEN) {
        len = TEXT_KEYER_MAX_LEN - 1;
    }

    memset(&s_send, 0, sizeof(s_send));
    memcpy(s_send.text, text, len);
    s_send.text[len] = '\0';
    s_send.text_len = len;

    /* Count actual characters (excluding spaces and prosign brackets) */
    s_send.char_index = 0;

    s_state = TEXT_KEYER_SENDING;
    return 0;
}

void text_keyer_abort(void) {
    if (s_state == TEXT_KEYER_IDLE) return;

    /* Always ensure key is released on abort */
    set_key_down(false);

    s_state = TEXT_KEYER_IDLE;
    memset(&s_send, 0, sizeof(s_send));
}

void text_keyer_pause(void) {
    if (s_state != TEXT_KEYER_SENDING) return;

    if (s_send.key_down) {
        set_key_down(false);
        s_send.key_down = false;
    }

    s_state = TEXT_KEYER_PAUSED;
}

void text_keyer_resume(void) {
    if (s_state != TEXT_KEYER_PAUSED) return;

    s_send.element_end_us = 0;  /* Restart timing */
    s_state = TEXT_KEYER_SENDING;
}

text_keyer_state_t text_keyer_get_state(void) {
    return s_state;
}

void text_keyer_get_progress(size_t *sent, size_t *total) {
    if (sent != NULL) {
        *sent = s_send.char_index;
    }
    if (total != NULL) {
        *total = s_send.text_len;
    }
}

void text_keyer_tick(int64_t now_us) {
    if (s_state != TEXT_KEYER_SENDING) return;

    /* Check paddle abort */
    if (s_paddle_abort != NULL &&
        atomic_load_explicit(s_paddle_abort, memory_order_acquire)) {
        text_keyer_abort();
        return;
    }

    /* First tick - start first element */
    if (s_send.element_end_us == 0) {
        if (!start_next_element(now_us)) {
            s_state = TEXT_KEYER_IDLE;
        }
        return;
    }

    /* Check if current element finished */
    if (now_us >= s_send.element_end_us) {
        finish_element(now_us);

        if (s_send.element_end_us == 0) {
            if (!start_next_element(now_us)) {
                s_state = TEXT_KEYER_IDLE;
                set_key_down(false);  /* Ensure key released when done */
            }
        }
    }
}

bool text_keyer_is_key_down(void) {
    return atomic_load_explicit(&s_key_down, memory_order_acquire);
}
