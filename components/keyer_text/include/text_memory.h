/**
 * @file text_memory.h
 * @brief Memory slots for stored text messages (NVS)
 *
 * 8 slots for frequently used messages (CQ, 73, contest exchanges).
 */

#ifndef KEYER_TEXT_MEMORY_H
#define KEYER_TEXT_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of memory slots */
#define TEXT_MEMORY_SLOTS 8

/** Maximum text length per slot */
#define TEXT_MEMORY_MAX_LEN 128

/** Maximum label length */
#define TEXT_MEMORY_LABEL_LEN 16

/**
 * @brief Memory slot structure
 */
typedef struct {
    char text[TEXT_MEMORY_MAX_LEN];
    char label[TEXT_MEMORY_LABEL_LEN];
} text_memory_slot_t;

/**
 * @brief Initialize text memory (load from NVS)
 *
 * @return 0 on success, -1 on error
 */
int text_memory_init(void);

/**
 * @brief Get slot contents
 *
 * @param slot Slot number (0-7)
 * @param out Output slot (can be NULL to just check if valid)
 * @return 0 on success, -1 if slot invalid or empty
 */
int text_memory_get(uint8_t slot, text_memory_slot_t *out);

/**
 * @brief Set slot contents
 *
 * @param slot Slot number (0-7)
 * @param text Text to store (NULL to clear)
 * @param label Label (NULL to keep existing or use default)
 * @return 0 on success, -1 on error
 */
int text_memory_set(uint8_t slot, const char *text, const char *label);

/**
 * @brief Clear slot
 *
 * @param slot Slot number (0-7)
 * @return 0 on success, -1 on error
 */
int text_memory_clear(uint8_t slot);

/**
 * @brief Set slot label only
 *
 * @param slot Slot number (0-7)
 * @param label New label
 * @return 0 on success, -1 on error
 */
int text_memory_set_label(uint8_t slot, const char *label);

/**
 * @brief Save all slots to NVS
 *
 * @return 0 on success, -1 on error
 */
int text_memory_save(void);

/**
 * @brief Check if slot has content
 *
 * @param slot Slot number (0-7)
 * @return true if slot has text, false if empty
 */
bool text_memory_is_set(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_TEXT_MEMORY_H */
