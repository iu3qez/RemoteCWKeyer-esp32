/**
 * @file iambic_preset.h
 * @brief Iambic keyer preset system
 *
 * Allows saving and recalling 10 named configurations with different
 * speeds, modes, and memory window settings.
 *
 * RT-Safe: Preset reads are <200ns, never block RT path.
 * Static allocation: No heap, all data compile-time sized.
 * Atomic operations: All configuration changes via atomics.
 *
 * @deprecated This preset system is deprecated in favor of unified g_config.
 *             Enum types (iambic_mode_t, memory_mode_t, squeeze_mode_t) still used.
 *             Future: NVS-backed presets integrated with g_config.
 */

#ifndef KEYER_IAMBIC_PRESET_H
#define KEYER_IAMBIC_PRESET_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Number of preset slots */
#define IAMBIC_PRESET_COUNT 10

/** Maximum preset name length (including null terminator) */
#define IAMBIC_PRESET_NAME_MAX 32

/** NVS schema version for migration support */
#define IAMBIC_PRESET_SCHEMA_VERSION 1

/* ============================================================================
 * Enums
 * ============================================================================ */

/**
 * @brief Iambic keyer mode
 */
typedef enum {
    IAMBIC_MODE_A = 0,  /**< Mode A: Stop immediately when paddles released */
    IAMBIC_MODE_B = 1,  /**< Mode B: Complete current + bonus element on squeeze release */
} iambic_mode_t;

/**
 * @brief Memory mode - which paddles are remembered during element transmission
 */
typedef enum {
    MEMORY_MODE_NONE       = 0,  /**< No memory - ignores all paddle input during element */
    MEMORY_MODE_DOT_ONLY   = 1,  /**< Remember dot paddle only */
    MEMORY_MODE_DAH_ONLY   = 2,  /**< Remember dah paddle only */
    MEMORY_MODE_DOT_AND_DAH = 3, /**< Remember both paddles (full iambic) */
} memory_mode_t;

/**
 * @brief Squeeze mode - when to sample paddle state for squeeze detection
 */
typedef enum {
    SQUEEZE_MODE_LATCH_OFF = 0,  /**< Live/immediate - check paddle state continuously */
    SQUEEZE_MODE_LATCH_ON  = 1,  /**< Snapshot/latch - capture at element start */
} squeeze_mode_t;

/* ============================================================================
 * Preset Structure
 * ============================================================================ */

/**
 * @brief Single iambic preset configuration
 *
 * All fields are atomic for RT-safe access.
 * Memory window parameters define when paddle input is memorized:
 *
 *   |<-------- Element Duration -------->|
 *   0%        U%                D%      100%
 *   |---------|-------------------|-------|
 *   | IGNORED |    MEMORIZED     |IGNORED|
 *
 * - Before U%: Paddle input ignored (debounce/false trigger)
 * - U% to D%: Paddle input memorized (valid squeeze window)
 * - After D%: Paddle input ignored (too late, next element)
 */
typedef struct {
    char name[IAMBIC_PRESET_NAME_MAX];  /**< User-defined name (empty = unused slot) */

    atomic_uint_fast32_t speed_wpm;     /**< Speed in WPM (5-100) */
    atomic_uint_fast8_t iambic_mode;    /**< iambic_mode_t value */
    atomic_uint_fast8_t memory_mode;    /**< memory_mode_t value */
    atomic_uint_fast8_t squeeze_mode;   /**< squeeze_mode_t value */

    atomic_uint_fast8_t mem_window_start_pct;  /**< Memory window start (0-100%) */
    atomic_uint_fast8_t mem_window_end_pct;    /**< Memory window end (0-100%) */
} iambic_preset_t;

/**
 * @brief Default preset values
 */
#define IAMBIC_PRESET_DEFAULT { \
    .name = "", \
    .speed_wpm = 25, \
    .iambic_mode = IAMBIC_MODE_B, \
    .memory_mode = MEMORY_MODE_DOT_AND_DAH, \
    .squeeze_mode = SQUEEZE_MODE_LATCH_OFF, \
    .mem_window_start_pct = 0, \
    .mem_window_end_pct = 100 \
}

/* ============================================================================
 * Preset System
 * ============================================================================ */

/**
 * @brief Global preset system state
 */
typedef struct {
    iambic_preset_t presets[IAMBIC_PRESET_COUNT];  /**< All preset slots */
    atomic_uint_fast32_t active_index;              /**< Currently active preset (0-9) */
} iambic_preset_system_t;

/** Global preset system instance */
extern iambic_preset_system_t g_iambic_presets;

/* ============================================================================
 * Preset System Functions
 * ============================================================================ */

/**
 * @brief Initialize preset system with defaults
 */
void iambic_preset_init(void);

/**
 * @brief Get currently active preset (RT-safe, <200ns)
 *
 * @return Pointer to active preset (never NULL)
 */
const iambic_preset_t* iambic_preset_active(void);

/**
 * @brief Get preset by index
 *
 * @param index Preset index (0-9)
 * @return Pointer to preset, or NULL if index invalid
 */
const iambic_preset_t* iambic_preset_get(uint32_t index);

/**
 * @brief Get mutable preset by index (for editing)
 *
 * @param index Preset index (0-9)
 * @return Pointer to preset, or NULL if index invalid
 */
iambic_preset_t* iambic_preset_get_mut(uint32_t index);

/**
 * @brief Switch to preset by index
 *
 * @param index Preset index (0-9)
 * @return true if switched successfully, false if index invalid
 */
bool iambic_preset_activate(uint32_t index);

/**
 * @brief Get active preset index
 *
 * @return Active preset index (0-9)
 */
uint32_t iambic_preset_active_index(void);

/**
 * @brief Copy preset to another slot
 *
 * @param src_index Source preset index
 * @param dst_index Destination preset index
 * @return true if copied successfully
 */
bool iambic_preset_copy(uint32_t src_index, uint32_t dst_index);

/**
 * @brief Reset preset to defaults
 *
 * @param index Preset index (0-9)
 * @return true if reset successfully
 */
bool iambic_preset_reset(uint32_t index);

/**
 * @brief Set preset name
 *
 * @param index Preset index (0-9)
 * @param name New name (will be truncated if too long)
 * @return true if set successfully
 */
bool iambic_preset_set_name(uint32_t index, const char* name);

/* ============================================================================
 * Preset Value Accessors (RT-safe)
 * ============================================================================ */

/**
 * @brief Get preset speed in WPM
 */
static inline uint32_t iambic_preset_get_wpm(const iambic_preset_t* preset) {
    return (uint32_t)atomic_load_explicit(&preset->speed_wpm, memory_order_relaxed);
}

/**
 * @brief Get preset iambic mode
 */
static inline iambic_mode_t iambic_preset_get_mode(const iambic_preset_t* preset) {
    return (iambic_mode_t)atomic_load_explicit(&preset->iambic_mode, memory_order_relaxed);
}

/**
 * @brief Get preset memory mode
 */
static inline memory_mode_t iambic_preset_get_memory_mode(const iambic_preset_t* preset) {
    return (memory_mode_t)atomic_load_explicit(&preset->memory_mode, memory_order_relaxed);
}

/**
 * @brief Get preset squeeze mode
 */
static inline squeeze_mode_t iambic_preset_get_squeeze_mode(const iambic_preset_t* preset) {
    return (squeeze_mode_t)atomic_load_explicit(&preset->squeeze_mode, memory_order_relaxed);
}

/**
 * @brief Get memory window start percentage
 */
static inline uint8_t iambic_preset_get_mem_start(const iambic_preset_t* preset) {
    return (uint8_t)atomic_load_explicit(&preset->mem_window_start_pct, memory_order_relaxed);
}

/**
 * @brief Get memory window end percentage
 */
static inline uint8_t iambic_preset_get_mem_end(const iambic_preset_t* preset) {
    return (uint8_t)atomic_load_explicit(&preset->mem_window_end_pct, memory_order_relaxed);
}

/* ============================================================================
 * Preset Value Setters
 * ============================================================================ */

/**
 * @brief Set preset speed in WPM
 */
static inline void iambic_preset_set_wpm(iambic_preset_t* preset, uint32_t wpm) {
    if (wpm >= 5 && wpm <= 100) {
        atomic_store_explicit(&preset->speed_wpm, wpm, memory_order_relaxed);
    }
}

/**
 * @brief Set preset iambic mode
 */
static inline void iambic_preset_set_mode(iambic_preset_t* preset, iambic_mode_t mode) {
    atomic_store_explicit(&preset->iambic_mode, (uint8_t)mode, memory_order_relaxed);
}

/**
 * @brief Set preset memory mode
 */
static inline void iambic_preset_set_memory_mode(iambic_preset_t* preset, memory_mode_t mode) {
    atomic_store_explicit(&preset->memory_mode, (uint8_t)mode, memory_order_relaxed);
}

/**
 * @brief Set preset squeeze mode
 */
static inline void iambic_preset_set_squeeze_mode(iambic_preset_t* preset, squeeze_mode_t mode) {
    atomic_store_explicit(&preset->squeeze_mode, (uint8_t)mode, memory_order_relaxed);
}

/**
 * @brief Set memory window start percentage
 */
static inline void iambic_preset_set_mem_start(iambic_preset_t* preset, uint8_t pct) {
    if (pct <= 100) {
        atomic_store_explicit(&preset->mem_window_start_pct, pct, memory_order_relaxed);
    }
}

/**
 * @brief Set memory window end percentage
 */
static inline void iambic_preset_set_mem_end(iambic_preset_t* preset, uint8_t pct) {
    if (pct <= 100) {
        atomic_store_explicit(&preset->mem_window_end_pct, pct, memory_order_relaxed);
    }
}

/* ============================================================================
 * Timing Calculation Helpers
 * ============================================================================ */

/**
 * @brief Calculate dit duration in microseconds from WPM
 *
 * PARIS timing: dit = 1.2 / WPM seconds = 1,200,000 / WPM microseconds
 *
 * @param wpm Speed in words per minute
 * @return Dit duration in microseconds
 */
static inline int64_t iambic_wpm_to_dit_us(uint32_t wpm) {
    if (wpm == 0) return 0;
    return 1200000 / (int64_t)wpm;
}

/**
 * @brief Check if progress is within memory window
 *
 * @param progress_pct Current element progress (0-100%)
 * @param start_pct Window start percentage
 * @param end_pct Window end percentage
 * @return true if within memory window
 */
static inline bool iambic_in_memory_window(uint8_t progress_pct, uint8_t start_pct, uint8_t end_pct) {
    return (progress_pct >= start_pct) && (progress_pct <= end_pct);
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_IAMBIC_PRESET_H */
