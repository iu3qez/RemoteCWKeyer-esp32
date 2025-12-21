/**
 * @file audio_source.c
 * @brief Audio source selector implementation
 */

#include "audio_source.h"
#include <assert.h>

void audio_source_init(audio_source_selector_t *sel) {
    assert(sel != NULL);

    sel->current = AUDIO_SOURCE_NONE;
    sel->sidetone_active = false;
    sel->remote_active = false;
}

void audio_source_set_sidetone(audio_source_selector_t *sel, bool active) {
    assert(sel != NULL);
    sel->sidetone_active = active;
}

void audio_source_set_remote(audio_source_selector_t *sel, bool active) {
    assert(sel != NULL);
    sel->remote_active = active;
}

audio_source_t audio_source_update(audio_source_selector_t *sel) {
    assert(sel != NULL);

    /* Priority: sidetone > remote > none */
    if (sel->sidetone_active) {
        sel->current = AUDIO_SOURCE_SIDETONE;
    } else if (sel->remote_active) {
        sel->current = AUDIO_SOURCE_REMOTE;
    } else {
        sel->current = AUDIO_SOURCE_NONE;
    }

    return sel->current;
}
