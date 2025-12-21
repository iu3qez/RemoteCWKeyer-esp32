/**
 * @file audio_source.h
 * @brief Audio source selector (sidetone vs remote)
 *
 * Manages switching between local sidetone and remote audio.
 */

#ifndef KEYER_AUDIO_SOURCE_H
#define KEYER_AUDIO_SOURCE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio source type
 */
typedef enum {
    AUDIO_SOURCE_NONE = 0,     /**< No audio */
    AUDIO_SOURCE_SIDETONE = 1, /**< Local sidetone */
    AUDIO_SOURCE_REMOTE = 2,   /**< Remote CW audio */
} audio_source_t;

/**
 * @brief Audio source selector
 */
typedef struct {
    audio_source_t current;    /**< Current active source */
    bool sidetone_active;      /**< Sidetone requested */
    bool remote_active;        /**< Remote audio requested */
} audio_source_selector_t;

/**
 * @brief Initialize audio source selector
 *
 * @param sel Selector to initialize
 */
void audio_source_init(audio_source_selector_t *sel);

/**
 * @brief Set sidetone active state
 *
 * @param sel Selector
 * @param active true to request sidetone
 */
void audio_source_set_sidetone(audio_source_selector_t *sel, bool active);

/**
 * @brief Set remote audio active state
 *
 * @param sel Selector
 * @param active true to request remote audio
 */
void audio_source_set_remote(audio_source_selector_t *sel, bool active);

/**
 * @brief Update source selection
 *
 * Call after setting sidetone/remote states.
 * Priority: sidetone > remote > none
 *
 * @param sel Selector
 * @return Current active source
 */
audio_source_t audio_source_update(audio_source_selector_t *sel);

/**
 * @brief Get current audio source
 *
 * @param sel Selector
 * @return Current active source
 */
static inline audio_source_t audio_source_get(const audio_source_selector_t *sel) {
    return sel->current;
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_AUDIO_SOURCE_H */
