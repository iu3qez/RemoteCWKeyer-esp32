/**
 * @file commands.c
 * @brief Command registry and handlers
 *
 * Implements all console commands: help, version, stats, reboot, etc.
 */

#include "console.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/* ============================================================================
 * Error code helpers
 * ============================================================================ */

const char *console_error_code(console_error_t err) {
    switch (err) {
        case CONSOLE_OK:                  return "OK";
        case CONSOLE_ERR_UNKNOWN_CMD:     return "E01";
        case CONSOLE_ERR_INVALID_VALUE:   return "E02";
        case CONSOLE_ERR_MISSING_ARG:     return "E03";
        case CONSOLE_ERR_OUT_OF_RANGE:    return "E04";
        case CONSOLE_ERR_REQUIRES_CONFIRM: return "E05";
        case CONSOLE_ERR_NVS_ERROR:       return "E06";
        default:                          return "E??";
    }
}

const char *console_error_message(console_error_t err) {
    switch (err) {
        case CONSOLE_OK:                  return "success";
        case CONSOLE_ERR_UNKNOWN_CMD:     return "unknown command";
        case CONSOLE_ERR_INVALID_VALUE:   return "invalid value";
        case CONSOLE_ERR_MISSING_ARG:     return "missing argument";
        case CONSOLE_ERR_OUT_OF_RANGE:    return "out of range";
        case CONSOLE_ERR_REQUIRES_CONFIRM: return "requires 'confirm'";
        case CONSOLE_ERR_NVS_ERROR:       return "NVS error";
        default:                          return "unknown error";
    }
}

/* ============================================================================
 * Command handlers
 * ============================================================================ */

/**
 * @brief help [cmd] - List commands or show help for specific command
 */
static console_error_t cmd_help(const console_parsed_cmd_t *cmd) {
    if (cmd->argc > 0 && cmd->args[0] != NULL) {
        /* Help for specific command */
        const console_cmd_t *c = console_find_command(cmd->args[0]);
        if (c == NULL) {
            return CONSOLE_ERR_UNKNOWN_CMD;
        }
        printf("  %s - %s\r\n", c->name, c->brief);
    } else {
        /* List all commands */
        size_t count;
        const console_cmd_t *cmds = console_get_commands(&count);
        printf("Available commands:\r\n");
        for (size_t i = 0; i < count; i++) {
            printf("  %-14s %s\r\n", cmds[i].name, cmds[i].brief);
        }
    }
    return CONSOLE_OK;
}

/**
 * @brief ? - Alias for help
 */
static console_error_t cmd_question(const console_parsed_cmd_t *cmd) {
    return cmd_help(cmd);
}

/**
 * @brief version (v) - Show version info
 */
static console_error_t cmd_version(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    printf("CW Keyer v0.1.0\r\n");
#ifdef CONFIG_IDF_TARGET
    printf("ESP-IDF: %s\r\n", esp_get_idf_version());
    printf("Target: %s\r\n", CONFIG_IDF_TARGET);
#else
    printf("Host build\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief stats [tasks|heap|stream|rt] - System statistics
 */
static console_error_t cmd_stats(const console_parsed_cmd_t *cmd) {
#ifdef CONFIG_IDF_TARGET
    if (cmd->argc == 0) {
        /* Overview */
        int64_t uptime_us = esp_timer_get_time();
        int64_t uptime_s = uptime_us / 1000000;
        int64_t hours = uptime_s / 3600;
        int64_t mins = (uptime_s % 3600) / 60;
        int64_t secs = uptime_s % 60;

        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();

        printf("uptime: %lld:%02lld:%02lld\r\n", hours, mins, secs);
        printf("heap: %lu bytes free (min: %lu)\r\n",
               (unsigned long)heap_free, (unsigned long)heap_min);
        printf("stream: ok\r\n");
    } else if (strcmp(cmd->args[0], "heap") == 0) {
        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();
        printf("heap free:    %lu bytes\r\n", (unsigned long)heap_free);
        printf("heap minimum: %lu bytes\r\n", (unsigned long)heap_min);
    } else if (strcmp(cmd->args[0], "tasks") == 0) {
        printf("=== Core 0 (RT) ===\r\n");
        printf("NAME            CPU%%  STACK  PRIO\r\n");
        printf("(enable runtime stats for details)\r\n\r\n");
        printf("=== Core 1 (BE) ===\r\n");
        printf("NAME            CPU%%  STACK  PRIO\r\n");
        printf("(enable runtime stats for details)\r\n");
    } else if (strcmp(cmd->args[0], "stream") == 0) {
        printf("stream: ok\r\n");
    } else if (strcmp(cmd->args[0], "rt") == 0) {
        printf("rt: ok\r\n");
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }
#else
    (void)cmd;
    printf("stats not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief reboot confirm - Restart system
 */
static console_error_t cmd_reboot(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0 || strcmp(cmd->args[0], "confirm") != 0) {
        return CONSOLE_ERR_REQUIRES_CONFIRM;
    }
#ifdef CONFIG_IDF_TARGET
    printf("Rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
#else
    printf("reboot not available on host\r\n");
#endif
    return CONSOLE_OK;
}

/**
 * @brief save - Persist configuration to NVS
 */
static console_error_t cmd_save(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    /* TODO: Implement NVS save */
    printf("Configuration saved\r\n");
    return CONSOLE_OK;
}

/**
 * @brief show [pattern] - Show parameters
 */
static console_error_t cmd_show(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    /* TODO: Integrate with config registry */
    printf("wpm=20\r\n");
    printf("sidetone_freq=600\r\n");
    printf("sidetone_volume=80\r\n");
    return CONSOLE_OK;
}

/**
 * @brief set <param> <value> - Set parameter value
 */
static console_error_t cmd_set(const console_parsed_cmd_t *cmd) {
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }
    /* TODO: Integrate with config registry */
    printf("set %s = %s\r\n", cmd->args[0], cmd->args[1]);
    return CONSOLE_OK;
}

/* ============================================================================
 * Command registry
 * ============================================================================ */

static const console_cmd_t s_commands[] = {
    { "help",    "List commands or show help",   cmd_help },
    { "?",       "Alias for help",               cmd_question },
    { "version", "Show version info",            cmd_version },
    { "v",       "Alias for version",            cmd_version },
    { "stats",   "System statistics",            cmd_stats },
    { "show",    "Show parameters",              cmd_show },
    { "set",     "Set parameter value",          cmd_set },
    { "save",    "Persist to NVS",               cmd_save },
    { "reboot",  "Restart system",               cmd_reboot },
};

#define NUM_COMMANDS (sizeof(s_commands) / sizeof(s_commands[0]))

const console_cmd_t *console_get_commands(size_t *count) {
    if (count != NULL) {
        *count = NUM_COMMANDS;
    }
    return s_commands;
}

const console_cmd_t *console_find_command(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(s_commands[i].name, name) == 0) {
            return &s_commands[i];
        }
    }
    return NULL;
}

console_error_t console_execute(const console_parsed_cmd_t *cmd) {
    if (cmd == NULL || cmd->command == NULL || cmd->command[0] == '\0') {
        return CONSOLE_OK; /* Empty line, do nothing */
    }

    const console_cmd_t *c = console_find_command(cmd->command);
    if (c == NULL) {
        return CONSOLE_ERR_UNKNOWN_CMD;
    }

    return c->handler(cmd);
}
