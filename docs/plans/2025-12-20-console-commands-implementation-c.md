# Console Commands Implementation - C (ESP-IDF)

**Date**: 2025-12-21
**Status**: Implemented
**Language**: C (ESP-IDF)
**Based on**: 2025-12-20-console-commands-design.md (Rust design)

## Overview

UART serial console implementation in C following the original Rust design. The system provides command parsing, history, and extensible command registry with zero heap allocation.

## Implementation Summary

### Files Created/Modified

| File | Description |
|------|-------------|
| `components/keyer_console/include/console.h` | Console API declarations |
| `components/keyer_console/src/parser.c` | Command line tokenizer |
| `components/keyer_console/src/commands.c` | Command registry and handlers |
| `components/keyer_console/src/console.c` | Main console loop |
| `test_host/test_console_parser.c` | Host unit tests |

## Error Codes

```c
typedef enum {
    CONSOLE_OK = 0,
    CONSOLE_ERR_UNKNOWN_CMD,    /* E01: Unknown command */
    CONSOLE_ERR_INVALID_VALUE,  /* E02: Invalid value */
    CONSOLE_ERR_MISSING_ARG,    /* E03: Missing argument */
    CONSOLE_ERR_OUT_OF_RANGE,   /* E04: Out of range */
    CONSOLE_ERR_REQUIRES_CONFIRM, /* E05: Requires 'confirm' */
    CONSOLE_ERR_NVS_ERROR,      /* E06: NVS error */
} console_error_t;
```

## Parser

### Parsed Command Structure

```c
#define CONSOLE_MAX_ARGS 3

typedef struct {
    const char *command;                /* Command name (first token) */
    const char *args[CONSOLE_MAX_ARGS]; /* Arguments (up to 3) */
    int argc;                           /* Number of arguments */
} console_parsed_cmd_t;
```

### Parser Implementation

```c
void console_parse_line(const char *line, console_parsed_cmd_t *out) {
    /* Initialize output */
    out->command = "";
    out->argc = 0;
    for (int i = 0; i < CONSOLE_MAX_ARGS; i++) {
        out->args[i] = NULL;
    }

    if (line == NULL || *line == '\0') {
        return;
    }

    /* Static buffer for parsed tokens */
    static char s_parse_buf[CONSOLE_LINE_MAX];
    strncpy(s_parse_buf, line, CONSOLE_LINE_MAX - 1);
    s_parse_buf[CONSOLE_LINE_MAX - 1] = '\0';

    char *p = s_parse_buf;

    /* Skip leading whitespace */
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        return;
    }

    /* Extract command */
    out->command = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }

    if (*p != '\0') {
        *p = '\0';
        p++;
    }

    /* Extract arguments */
    for (int i = 0; i < CONSOLE_MAX_ARGS && *p != '\0'; i++) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        out->args[i] = p;
        out->argc++;

        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }

        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
}
```

## Command Registry

### Command Descriptor

```c
typedef console_error_t (*console_cmd_handler_t)(const console_parsed_cmd_t *cmd);

typedef struct {
    const char *name;              /* Command name */
    const char *brief;             /* Brief description */
    console_cmd_handler_t handler; /* Handler function */
} console_cmd_t;
```

### Available Commands

| Command | Description |
|---------|-------------|
| `help` | List commands or show command help |
| `set` | Set parameter value |
| `show` | Show parameter values |
| `stats` | System statistics |
| `reboot` | Restart system (requires confirm) |
| `save` | Save config to NVS |

### Command Execution

```c
console_error_t console_execute(const console_parsed_cmd_t *cmd) {
    if (cmd->command == NULL || cmd->command[0] == '\0') {
        return CONSOLE_OK;  /* Empty line */
    }

    const console_cmd_t *handler = console_find_command(cmd->command);
    if (handler == NULL) {
        return CONSOLE_ERR_UNKNOWN_CMD;
    }

    return handler->handler(cmd);
}
```

## Buffer Constants

```c
#define CONSOLE_LINE_MAX     64   /* Maximum command line length */
#define CONSOLE_HISTORY_SIZE 4    /* History depth */
#define CONSOLE_MAX_ARGS     3    /* Maximum arguments per command */
```

## Unit Tests

All console parser tests pass:

1. `test_parse_empty_line` - Empty input handling
2. `test_parse_simple_command` - Single command
3. `test_parse_command_with_one_arg` - Command + 1 arg
4. `test_parse_command_with_two_args` - Command + 2 args
5. `test_parse_command_with_three_args` - Command + 3 args
6. `test_parse_extra_args_ignored` - Extra args dropped
7. `test_parse_leading_whitespace` - Leading space handling
8. `test_parse_trailing_whitespace` - Trailing space handling
9. `test_parse_multiple_spaces` - Multiple space handling

## Compliance with ARCHITECTURE.md

- **No heap allocation** - Static parse buffer
- **Core 1** - Console runs on background core
- **Non-blocking** - Character-by-character processing
- **Testable** - Unity tests run on host without hardware

## Example Usage

```
> help
  help           List commands
  set            Set parameter value
  show           Show parameter values
  stats          System statistics
  reboot         Restart system
  save           Save config to NVS

> set wpm 25
OK

> show wpm
wpm=25

> reboot
E05: requires 'confirm'

> reboot confirm
(system reboots)
```

## Future Work

1. **Tab Completion** - Complete command and parameter names
2. **History** - Arrow key navigation through command history
3. **Preset Commands** - `preset load N`, `preset save N`
4. **Debug Commands** - Log level control, diagnostics
