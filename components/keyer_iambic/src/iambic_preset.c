/**
 * @file iambic_preset.c
 * @brief Iambic keyer preset system implementation
 */

#include "iambic_preset.h"
#include <string.h>

/* ============================================================================
 * Global Preset System Instance
 * ============================================================================ */

/**
 * Default preset names for built-in presets
 */
static const char* const DEFAULT_PRESET_NAMES[IAMBIC_PRESET_COUNT] = {
    "Default",      /* 0: Standard settings */
    "Contest",      /* 1: Fast contest operation */
    "Slow",         /* 2: Learning/slow operation */
    "QRS",          /* 3: Very slow for beginners */
    "",             /* 4: Empty slot */
    "",             /* 5: Empty slot */
    "",             /* 6: Empty slot */
    "",             /* 7: Empty slot */
    "",             /* 8: Empty slot */
    "",             /* 9: Empty slot */
};

/**
 * Default WPM values for built-in presets
 */
static const uint32_t DEFAULT_PRESET_WPM[IAMBIC_PRESET_COUNT] = {
    25,     /* 0: Default - standard speed */
    35,     /* 1: Contest - fast */
    15,     /* 2: Slow - relaxed */
    10,     /* 3: QRS - very slow */
    25, 25, 25, 25, 25, 25  /* 4-9: Default speed */
};

/**
 * Global preset system - statically allocated
 * Note: Zero-initialized by C standard, then properly initialized by iambic_preset_init()
 */
iambic_preset_system_t g_iambic_presets;

/* ============================================================================
 * Initialization
 * ============================================================================ */

void iambic_preset_init(void) {
    for (uint32_t i = 0; i < IAMBIC_PRESET_COUNT; i++) {
        iambic_preset_t* preset = &g_iambic_presets.presets[i];

        /* Set name */
        strncpy(preset->name, DEFAULT_PRESET_NAMES[i], IAMBIC_PRESET_NAME_MAX - 1);
        preset->name[IAMBIC_PRESET_NAME_MAX - 1] = '\0';

        /* Set speed */
        atomic_store_explicit(&preset->speed_wpm, DEFAULT_PRESET_WPM[i], memory_order_relaxed);

        /* Set default modes */
        atomic_store_explicit(&preset->iambic_mode, IAMBIC_MODE_B, memory_order_relaxed);
        atomic_store_explicit(&preset->memory_mode, MEMORY_MODE_DOT_AND_DAH, memory_order_relaxed);
        atomic_store_explicit(&preset->squeeze_mode, SQUEEZE_MODE_LATCH_OFF, memory_order_relaxed);

        /* Set default memory window (0% - 100% = full window, compatible with legacy) */
        atomic_store_explicit(&preset->mem_window_start_pct, 0, memory_order_relaxed);
        atomic_store_explicit(&preset->mem_window_end_pct, 100, memory_order_relaxed);
    }

    /* Start with first preset active */
    atomic_store_explicit(&g_iambic_presets.active_index, 0, memory_order_relaxed);
}

/* ============================================================================
 * Preset Access Functions
 * ============================================================================ */

const iambic_preset_t* iambic_preset_active(void) {
    uint32_t idx = (uint32_t)atomic_load_explicit(&g_iambic_presets.active_index, memory_order_relaxed);
    if (idx >= IAMBIC_PRESET_COUNT) {
        idx = 0;  /* Safety fallback */
    }
    return &g_iambic_presets.presets[idx];
}

const iambic_preset_t* iambic_preset_get(uint32_t index) {
    if (index >= IAMBIC_PRESET_COUNT) {
        return NULL;
    }
    return &g_iambic_presets.presets[index];
}

iambic_preset_t* iambic_preset_get_mut(uint32_t index) {
    if (index >= IAMBIC_PRESET_COUNT) {
        return NULL;
    }
    return &g_iambic_presets.presets[index];
}

bool iambic_preset_activate(uint32_t index) {
    if (index >= IAMBIC_PRESET_COUNT) {
        return false;
    }
    atomic_store_explicit(&g_iambic_presets.active_index, index, memory_order_release);
    return true;
}

uint32_t iambic_preset_active_index(void) {
    return (uint32_t)atomic_load_explicit(&g_iambic_presets.active_index, memory_order_relaxed);
}

/* ============================================================================
 * Preset Manipulation Functions
 * ============================================================================ */

bool iambic_preset_copy(uint32_t src_index, uint32_t dst_index) {
    if (src_index >= IAMBIC_PRESET_COUNT || dst_index >= IAMBIC_PRESET_COUNT) {
        return false;
    }
    if (src_index == dst_index) {
        return true;  /* No-op */
    }

    const iambic_preset_t* src = &g_iambic_presets.presets[src_index];
    iambic_preset_t* dst = &g_iambic_presets.presets[dst_index];

    /* Copy name */
    strncpy(dst->name, src->name, IAMBIC_PRESET_NAME_MAX);

    /* Copy atomic values */
    atomic_store_explicit(&dst->speed_wpm,
        atomic_load_explicit(&src->speed_wpm, memory_order_relaxed),
        memory_order_relaxed);

    atomic_store_explicit(&dst->iambic_mode,
        atomic_load_explicit(&src->iambic_mode, memory_order_relaxed),
        memory_order_relaxed);

    atomic_store_explicit(&dst->memory_mode,
        atomic_load_explicit(&src->memory_mode, memory_order_relaxed),
        memory_order_relaxed);

    atomic_store_explicit(&dst->squeeze_mode,
        atomic_load_explicit(&src->squeeze_mode, memory_order_relaxed),
        memory_order_relaxed);

    atomic_store_explicit(&dst->mem_window_start_pct,
        atomic_load_explicit(&src->mem_window_start_pct, memory_order_relaxed),
        memory_order_relaxed);

    atomic_store_explicit(&dst->mem_window_end_pct,
        atomic_load_explicit(&src->mem_window_end_pct, memory_order_relaxed),
        memory_order_relaxed);

    return true;
}

bool iambic_preset_reset(uint32_t index) {
    if (index >= IAMBIC_PRESET_COUNT) {
        return false;
    }

    iambic_preset_t* preset = &g_iambic_presets.presets[index];

    /* Reset name */
    strncpy(preset->name, DEFAULT_PRESET_NAMES[index], IAMBIC_PRESET_NAME_MAX - 1);
    preset->name[IAMBIC_PRESET_NAME_MAX - 1] = '\0';

    /* Reset values to defaults */
    atomic_store_explicit(&preset->speed_wpm, DEFAULT_PRESET_WPM[index], memory_order_relaxed);
    atomic_store_explicit(&preset->iambic_mode, IAMBIC_MODE_B, memory_order_relaxed);
    atomic_store_explicit(&preset->memory_mode, MEMORY_MODE_DOT_AND_DAH, memory_order_relaxed);
    atomic_store_explicit(&preset->squeeze_mode, SQUEEZE_MODE_LATCH_ON, memory_order_relaxed);
    atomic_store_explicit(&preset->mem_window_start_pct, 60, memory_order_relaxed);
    atomic_store_explicit(&preset->mem_window_end_pct, 99, memory_order_relaxed);

    return true;
}

bool iambic_preset_set_name(uint32_t index, const char* name) {
    if (index >= IAMBIC_PRESET_COUNT || name == NULL) {
        return false;
    }

    iambic_preset_t* preset = &g_iambic_presets.presets[index];
    strncpy(preset->name, name, IAMBIC_PRESET_NAME_MAX - 1);
    preset->name[IAMBIC_PRESET_NAME_MAX - 1] = '\0';

    return true;
}
