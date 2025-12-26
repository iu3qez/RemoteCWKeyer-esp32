/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_meta.h
 * @brief GUI metadata for all configuration parameters
 */

#ifndef KEYER_CONFIG_META_H
#define KEYER_CONFIG_META_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Widget type for parameter UI */
typedef enum {
    WIDGET_SLIDER = 0,
    WIDGET_SPINBOX = 1,
    WIDGET_DROPDOWN = 2,
    WIDGET_TOGGLE = 3,
    WIDGET_TEXTBOX = 4,
} widget_type_t;

/** When parameter changes can be applied */
typedef enum {
    RUNTIME_IMMEDIATE = 0,  /**< Applied immediately */
    RUNTIME_IDLE_ONLY = 1,  /**< Applied when paddles idle */
    RUNTIME_REBOOT = 2,     /**< Requires reboot */
} runtime_change_t;

/** Parameter metadata */
typedef struct {
    const char *name;
    const char *label_short_en;
    const char *label_long_en;
    const char *description_en;
    const char *category;
    uint8_t priority;
    bool advanced;
    widget_type_t widget;
    runtime_change_t runtime_change;
} param_meta_t;

/** Number of parameters */
#define PARAM_COUNT 29

/** All parameter metadata */
extern const param_meta_t PARAM_METADATA[PARAM_COUNT];

/** Get metadata by name */
const param_meta_t *config_get_meta(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_META_H */
