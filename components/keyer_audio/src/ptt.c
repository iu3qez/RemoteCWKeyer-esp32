/**
 * @file ptt.c
 * @brief PTT controller implementation
 */

#include "ptt.h"
#include <assert.h>

void ptt_init(ptt_controller_t *ptt, uint32_t tail_ms) {
    assert(ptt != NULL);

    ptt->state = PTT_OFF;
    ptt->tail_us = (uint64_t)tail_ms * 1000;
    ptt->last_audio_us = 0;
    ptt->audio_active = false;
}

void ptt_audio_sample(ptt_controller_t *ptt, uint64_t timestamp_us) {
    assert(ptt != NULL);

    ptt->last_audio_us = timestamp_us;
    ptt->audio_active = true;

    /* Turn on PTT immediately on audio */
    if (ptt->state == PTT_OFF) {
        ptt->state = PTT_ON;
    }
}

void ptt_tick(ptt_controller_t *ptt, uint64_t timestamp_us) {
    assert(ptt != NULL);

    if (ptt->state == PTT_ON) {
        /* Check if tail timeout expired */
        if (!ptt->audio_active && timestamp_us > ptt->last_audio_us + ptt->tail_us) {
            ptt->state = PTT_OFF;
        }
    }

    /* Reset audio_active flag - will be set again by audio_sample if active */
    ptt->audio_active = false;
}

void ptt_force_off(ptt_controller_t *ptt) {
    assert(ptt != NULL);

    ptt->state = PTT_OFF;
    ptt->audio_active = false;
}

void ptt_set_tail(ptt_controller_t *ptt, uint32_t tail_ms) {
    assert(ptt != NULL);

    ptt->tail_us = (uint64_t)tail_ms * 1000;
}
