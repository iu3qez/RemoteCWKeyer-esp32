/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.h
 * @brief Console command parameter registry with family support
 */

#ifndef KEYER_CONFIG_CONSOLE_H
#define KEYER_CONFIG_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parameter value union */
typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    bool     b;
    const char *str;  /**< For string parameters (points to config buffer) */
} param_value_t;

/** Parameter type with validation */
typedef enum {
    PARAM_TYPE_U8 = 0,
    PARAM_TYPE_U16 = 1,
    PARAM_TYPE_U32 = 2,
    PARAM_TYPE_BOOL = 3,
    PARAM_TYPE_ENUM = 4,
    PARAM_TYPE_STRING = 5,
} param_type_t;

/** Family descriptor */
typedef struct {
    const char *name;
    const char *aliases;      /**< Comma-separated: "k" or "a,snd" */
    const char *description;
    uint8_t order;
} family_descriptor_t;

/** Parameter descriptor */
typedef struct {
    const char *name;
    const char *family;
    const char *full_path;    /**< "keyer.wpm" */
    param_type_t type;
    uint32_t min;
    uint32_t max;
    param_value_t (*get_fn)(void);
    void (*set_fn)(param_value_t);
} param_descriptor_t;

#define FAMILY_COUNT 7
#define CONSOLE_PARAM_COUNT 30

extern const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT];
extern const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT];

/** Find family by name or alias */
const family_descriptor_t *config_find_family(const char *name);

/** Find parameter by name or full path */
const param_descriptor_t *config_find_param(const char *name);

/** Get parameter value as string */
int config_get_param_str(const char *name, char *buf, size_t len);

/** Set parameter from string */
int config_set_param_str(const char *name, const char *value);

/** Pattern matching visitor callback */
typedef void (*param_visitor_fn)(const param_descriptor_t *param, void *ctx);

/**
 * @brief Visit all parameters matching pattern
 *
 * Patterns:
 * - "keyer.wpm"     exact match
 * - "keyer.*"       all direct params in keyer
 * - "keyer.**"      all params in keyer and subfamilies
 * - "hw.*"          alias expansion + wildcard
 */
void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_CONSOLE_H */
