/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.h
 * @brief Console command parameter registry
 */

#ifndef KEYER_CONFIG_CONSOLE_H
#define KEYER_CONFIG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parameter value union */
typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    bool     b;
} param_value_t;

/** Parameter type with validation */
typedef enum {
    PARAM_TYPE_U8 = 0,
    PARAM_TYPE_U16 = 1,
    PARAM_TYPE_U32 = 2,
    PARAM_TYPE_BOOL = 3,
    PARAM_TYPE_ENUM = 4,
} param_type_t;

/** Parameter descriptor */
typedef struct {
    const char *name;
    const char *category;
    param_type_t type;
    uint32_t min;
    uint32_t max;
    param_value_t (*get_fn)(void);
    void (*set_fn)(param_value_t);
} param_descriptor_t;

/** Number of parameters */
#define CONSOLE_PARAM_COUNT 14

/** All parameter descriptors */
extern const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT];

/** Number of categories */
#define CATEGORY_COUNT 5

/** All categories */
extern const char *CATEGORIES[CATEGORY_COUNT];

/** Find parameter by name */
const param_descriptor_t *config_find_param(const char *name);

/** Get parameter value as string */
int config_get_param_str(const char *name, char *buf, size_t len);

/** Set parameter from string */
int config_set_param_str(const char *name, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_CONSOLE_H */
