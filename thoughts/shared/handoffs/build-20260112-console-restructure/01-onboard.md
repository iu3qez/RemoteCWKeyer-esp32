# Console Restructure: Onboarding Analysis

**Date**: 2026-01-12
**Component**: `/workspaces/RustRemoteCWKeyer/components/keyer_console/`
**Purpose**: Analyze current architecture before modularizing command handlers

---

## Executive Summary

The keyer_console component implements an interactive serial console with 31 commands. All command handlers currently live in a single 1296-line `commands.c` file. This analysis identifies refactoring opportunities for:

1. **Modularization**: Extract command families into separate files
2. **Subcommand Framework**: Replace manual strcmp patterns with declarative subcommand dispatch
3. **Code Reuse**: Eliminate duplication (e.g., m1-m8 handlers)

---

## File Inventory

| File | Lines | Purpose |
|------|-------|---------|
| `src/commands.c` | 1296 | Command registry and all handlers |
| `src/completion.c` | 367 | Tab completion (commands, params, subcommands) |
| `src/console.c` | 227 | Main console loop, line editing, escape sequences |
| `include/console.h` | 212 | Public API, command descriptor, error codes |
| `src/history.c` | 112 | Ring buffer for command history (4 entries) |
| `src/parser.c` | 100 | Tokenizer (splits on whitespace, max 3 args) |
| `src/line_buffer.c` | 8 | Stub (not implemented) |
| `include/log_tags.h` | 41 | Auto-generated ESP_LOG tag list |
| **Total** | **2363** | |

---

## Dependencies (CMakeLists.txt)

### REQUIRES (Public)
- `keyer_config` - Generated config from parameters.yaml
- `keyer_logging` - RT-safe logging (RT_INFO, RT_ERROR, etc.)
- `driver` - ESP-IDF GPIO/USB drivers
- `keyer_hal` - GPIO abstraction (hal_gpio_read_paddles)
- `keyer_decoder` - CW decoder (decoder_get_wpm, decoder_set_enabled)
- `keyer_text` - Text keyer and memory slots

### PRIV_REQUIRES (Private)
- `keyer_usb` - USB console printf, CDC ports
- `keyer_wifi` - WiFi status queries

---

## Command Registry (31 commands)

### Current Structure

Commands defined in static array `s_commands[]` at line 1224:

```c
typedef struct {
    const char *name;
    const char *brief;
    const char *usage;  /* NULL or multi-line help text */
    console_cmd_handler_t handler;
} console_cmd_t;
```

### Command Catalog by Category

#### System Info (5 commands)
| Command | Lines | Handler | Subcommands |
|---------|-------|---------|-------------|
| `help` | 89-132 | cmd_help | - |
| `?` | 137-139 | cmd_question | (alias) |
| `version` | 144-154 | cmd_version | - |
| `v` | - | cmd_version | (alias) |
| `stats` | 159-252 | cmd_stats | **heap, tasks, stream, rt** |

**Subcommand Pattern (stats)**:
```c
if (cmd->argc == 0) { /* overview */ }
else if (strcmp(cmd->args[0], "heap") == 0) { /* ... */ }
else if (strcmp(cmd->args[0], "tasks") == 0) { /* ... */ }
// Manual strcmp chain
```

#### Configuration (3 commands)
| Command | Lines | Handler | Pattern |
|---------|-------|---------|---------|
| `show` | 306-318 | cmd_show | Pattern matching (**, *, path) |
| `set` | 349-420 | cmd_set | key=value or key value |
| `save` | 274-286 | cmd_save | Persists to NVS |

**Complexity**: `set` handles 3 syntaxes:
- `set keyer.wpm 25`
- `set keyer.wpm=25`
- `set keyer.wpm = 25`
- `set wpm 25` (legacy name lookup)

#### System Control (3 commands)
| Command | Lines | Handler | Requires Confirm |
|---------|-------|---------|------------------|
| `reboot` | 257-269 | cmd_reboot | Yes |
| `factory-reset` | 525-547 | cmd_factory_reset | Yes |
| `uf2` | 509-520 | cmd_uf2 | No |
| `flash` | - | cmd_uf2 | (alias) |

**Pattern**: Dangerous commands require `confirm` argument:
```c
if (cmd->argc == 0 || strcmp(cmd->args[0], "confirm") != 0) {
    return CONSOLE_ERR_REQUIRES_CONFIRM;
}
```

#### Logging (3 commands)
| Command | Lines | Handler | Subcommands |
|---------|-------|---------|-------------|
| `log` | 425-504 | cmd_log | **level TAG LEVEL**, **TAG=L** |
| `debug` | 558-626 | cmd_debug | **info, none, TAG LEVEL** |
| `diag` | 631-650 | cmd_diag | **on, off** |

**Complexity**: `log` supports 2 syntaxes (ESP-IDF style and compact)

#### Diagnostics (3 commands)
| Command | Lines | Handler | Subcommands |
|---------|-------|---------|-------------|
| `gpio` | 655-786 | cmd_gpio | **init, PIN** |
| `test` | 791-877 | cmd_test | **cdc1, log, gpio** |
| `decoder` | 882-989 | cmd_decoder | **on, off, text, stats, clear** |

**Subcommand Count**: decoder has 5 subcommands

#### Text Keyer (6 commands)
| Command | Lines | Handler | Purpose |
|---------|-------|---------|---------|
| `send` | 998-1021 | cmd_send | Send text as CW |
| `abort` | 1053-1058 | cmd_abort | Stop transmission |
| `pause` | 1063-1068 | cmd_pause | Pause transmission |
| `resume` | 1073-1078 | cmd_resume | Resume transmission |
| `mem` | 1083-1146 | cmd_mem | **SLOT, SLOT TEXT, SLOT clear, SLOT label X** |
| `m1-m8` | 1026-1048 | cmd_memory_send | Send memory slot N |

**Code Duplication**: `m1-m8` are 8 separate command entries pointing to same handler

---

## Command Handlers: Line Ranges

| Handler Function | Lines | Size | Complexity |
|------------------|-------|------|------------|
| cmd_help | 89-132 | 44 | Medium (family lookup) |
| cmd_question | 137-139 | 3 | Trivial (alias) |
| cmd_version | 144-154 | 11 | Simple |
| cmd_stats | 159-252 | 94 | **High (4 subcommands + WiFi)** |
| cmd_reboot | 257-269 | 13 | Simple |
| cmd_save | 274-286 | 13 | Simple |
| cmd_show | 306-318 | 13 | Simple (delegates to config_foreach_matching) |
| cmd_set | 349-420 | 72 | **High (3 syntaxes + legacy lookup)** |
| cmd_log | 425-504 | 80 | **High (2 syntaxes + tag lookup)** |
| cmd_uf2 | 509-520 | 12 | Simple |
| cmd_factory_reset | 525-547 | 23 | Simple |
| cmd_debug | 558-626 | 69 | **High (5 subcommands + tags)** |
| cmd_diag | 631-650 | 20 | Simple |
| cmd_gpio | 655-786 | 132 | **VERY HIGH (2 subcommands + 10 read loops)** |
| cmd_test | 791-877 | 87 | **High (3 subcommands)** |
| cmd_decoder | 882-989 | 108 | **High (5 subcommands + timing stats)** |
| cmd_send | 998-1021 | 24 | Simple |
| cmd_memory_send | 1026-1048 | 23 | Simple |
| cmd_abort | 1053-1058 | 6 | Trivial |
| cmd_pause | 1063-1068 | 6 | Trivial |
| cmd_resume | 1073-1078 | 6 | Trivial |
| cmd_mem | 1083-1146 | 64 | **High (4 subcommands)** |

**Largest Handlers**:
1. `cmd_gpio` - 132 lines
2. `cmd_decoder` - 108 lines
3. `cmd_stats` - 94 lines
4. `cmd_test` - 87 lines

---

## Subcommand Patterns

### Commands with Manual Subcommand Dispatch

| Command | Subcommands | Pattern |
|---------|-------------|---------|
| `stats` | heap, tasks, stream, rt | strcmp chain (lines 197-246) |
| `debug` | info, none, TAG LEVEL | strcmp + arg parsing (lines 571-619) |
| `diag` | on, off | strcmp (lines 640-647) |
| `decoder` | on, off, clear, text, stats | strcmp chain (lines 917-986) |
| `mem` | SLOT, clear, label | Mixed: atoi + strcmp (lines 1098-1144) |
| `test` | cdc1, log, gpio | strcmp chain (lines 800-871) |
| `gpio` | init, PIN | Mixed: strcmp + atoi (lines 660-731) |

### Current Dispatch Pattern (Repeated 7 times)

```c
if (cmd->argc == 0) {
    /* Show status or default action */
}

const char *arg = cmd->args[0];

if (strcmp(arg, "subcmd1") == 0) {
    /* Handle subcmd1 */
    return CONSOLE_OK;
}
if (strcmp(arg, "subcmd2") == 0) {
    /* Handle subcmd2 */
    return CONSOLE_OK;
}
// ... more strcmp checks ...

return CONSOLE_ERR_INVALID_VALUE;
```

**Refactoring Opportunity**: Extract into declarative subcommand table

---

## Shared Helper Functions

### Used by Multiple Commands

| Helper | Users | Purpose |
|--------|-------|---------|
| `strip_quotes` (323-337) | set | Remove leading/trailing quotes |
| `show_command_help` (79-84) | help, execute | Print command usage |
| `show_param_visitor` (289-294) | show | Callback for config_foreach_matching |

### Missing Helpers (Code Duplication)

1. **Memory slot validation**: `cmd_mem` and `cmd_memory_send` both parse slot numbers
2. **Confirm pattern**: `cmd_reboot` and `cmd_factory_reset` both check for "confirm"
3. **Subcommand dispatch**: 7 commands manually strcmp their subcommands

---

## External Module Dependencies

### Commands grouped by external API usage

#### Keyer Config (config.h, config_console.h)
- `help` - config_find_family, CONSOLE_FAMILIES
- `show` - config_foreach_matching, config_get_param_str
- `set` - config_set_param_str, CONSOLE_PARAMS
- `save` - config_save_to_nvs

#### Keyer HAL (hal_gpio.h)
- `gpio` - hal_gpio_get_config, hal_gpio_read_paddles, gpio_from_paddles

#### Keyer Decoder (decoder.h)
- `decoder` - decoder_is_enabled, decoder_get_wpm, decoder_get_text, decoder_get_stats

#### Text Keyer (text_keyer.h, text_memory.h)
- `send` - text_keyer_send
- `abort` - text_keyer_abort
- `pause` - text_keyer_pause
- `resume` - text_keyer_resume
- `mem` - text_memory_get, text_memory_set, text_memory_clear
- `m1-m8` - text_memory_get, text_keyer_send

#### USB/WiFi (keyer_usb, keyer_wifi)
- `stats` - wifi_get_state, wifi_get_ip
- `test` - tinyusb_cdcacm_write_queue, usb_cdc_connected
- `uf2` - usb_uf2_enter

#### RT Logging (rt_log.h)
- `debug` - g_rt_log_stream, g_bg_log_stream, log_stream_count
- `test` - RT_INFO macro

---

## Dependency Graph (Commands → Modules)

```
config (show, set, save, help)
  └─ CONSOLE_PARAMS, CONSOLE_FAMILIES (generated)

hal_gpio (gpio)
  └─ GPIO hardware abstraction

decoder (decoder)
  └─ CW decoder state

text_keyer (send, abort, pause, resume)
text_memory (mem, m1-m8)
  └─ Text transmission subsystem

usb_cdc (test, uf2)
wifi (stats)
  └─ Connectivity

rt_log (debug, test)
  └─ Real-time logging
```

**Insight**: Commands naturally cluster by external module

---

## Tab Completion (completion.c)

### Completion Contexts

| Context | Triggered By | Matches Against |
|---------|--------------|-----------------|
| COMPLETE_COMMAND | First token | s_commands[] |
| COMPLETE_PARAM | After "set ", "show " | CONSOLE_PARAMS (with alias expansion) |
| COMPLETE_DIAG | After "diag " | "on", "off" |
| COMPLETE_DEBUG | After "debug " | LOG_TAGS[], "info", "none", "*", levels |
| COMPLETE_MEM_SLOT | After "mem " (first arg) | "1".."8" |
| COMPLETE_MEM_SUBCMD | After "mem N " (second arg) | "clear", "label" |

### Completion Helpers

| Function | Lines | Purpose |
|----------|-------|---------|
| get_matching_commands | 41-53 | Prefix match in s_commands[] |
| get_matching_params | 100-155 | Prefix match in CONSOLE_PARAMS (with k.→keyer. expansion) |
| get_matching_diag_args | 160-171 | Match "on" or "off" |
| get_matching_debug_args | 210-238 | Match LOG_TAGS + special cmds |
| get_matching_mem_slots | 176-187 | Match "1".."8" |
| get_matching_mem_subcmds | 192-203 | Match "clear", "label" |
| expand_family_alias | 62-89 | k→keyer, hw→hardware, etc. |

**Completion Strategy**: Show all matches on one line (line 345), complete to common prefix

---

## Pain Points & Refactoring Opportunities

### 1. Code Duplication: m1-m8 Commands

**Current**: 8 command entries in s_commands[], all pointing to `cmd_memory_send`

```c
{ "m1", "Send memory slot 1", NULL, cmd_memory_send },
{ "m2", "Send memory slot 2", NULL, cmd_memory_send },
// ... 6 more ...
```

**Handler extracts slot from name**:
```c
const char *name = cmd->command;
if (name[0] != 'm' || name[1] < '1' || name[1] > '8') {
    return CONSOLE_ERR_UNKNOWN_CMD;
}
uint8_t slot = (uint8_t)(name[1] - '1');
```

**Refactoring**: Generate these commands at compile-time with X-macro or loop-based registration

---

### 2. Manual Subcommand Dispatch (7 commands)

**Current Pattern** (repeated in stats, debug, decoder, test, mem, gpio, diag):

```c
if (cmd->argc == 0) { /* default */ }
if (strcmp(cmd->args[0], "subcmd1") == 0) { /* ... */ }
if (strcmp(cmd->args[0], "subcmd2") == 0) { /* ... */ }
// Manual strcmp chain
```

**Refactoring Opportunity**: Declarative subcommand table

```c
typedef struct {
    const char *name;
    console_error_t (*handler)(const console_parsed_cmd_t *parent);
} subcommand_t;

static const subcommand_t stats_subcmds[] = {
    { "heap", stats_heap },
    { "tasks", stats_tasks },
    { "stream", stats_stream },
    { "rt", stats_rt },
};

console_error_t dispatch_subcommands(const subcommand_t *subcmds, size_t count,
                                     const console_parsed_cmd_t *cmd);
```

---

### 3. Large Multi-Purpose Handlers

**cmd_gpio** (132 lines) does 3 things:
1. GPIO init subcommand (lines 660-701)
2. Test single pin (lines 704-731)
3. Read paddle state with debugging (lines 735-778)

**cmd_decoder** (108 lines) has 5 subcommands + default status

**Refactoring**: Split into separate handler functions per subcommand

---

### 4. Inconsistent Error Handling

Some commands return early:
```c
if (error) return CONSOLE_ERR_MISSING_ARG;
```

Others use switch on return codes:
```c
switch (ret) {
    case 0: return CONSOLE_OK;
    case -1: return CONSOLE_ERR_UNKNOWN_CMD;
    // ...
}
```

**Refactoring**: Consistent error mapping helper

---

### 5. No Grouping by Feature

All 31 commands in one file, no logical grouping by:
- System commands (reboot, version, stats)
- Config commands (show, set, save)
- Diagnostics (gpio, test, debug)
- CW commands (send, decoder, mem)

**Refactoring**: Split into `cmd_system.c`, `cmd_config.c`, `cmd_diagnostics.c`, `cmd_text.c`

---

### 6. Tight Coupling to printf

Every handler calls `printf` directly. Makes testing difficult without mocking.

**Refactoring**: Pass output stream (or callback) through command context

---

## Proposed File Structure (After Refactoring)

```
keyer_console/
├── src/
│   ├── console.c              # Main loop, line editing (unchanged)
│   ├── parser.c               # Tokenizer (unchanged)
│   ├── history.c              # Command history (unchanged)
│   ├── completion.c           # Tab completion (unchanged)
│   ├── command_registry.c     # NEW: Command registration + dispatch
│   ├── subcommand.c           # NEW: Generic subcommand dispatcher
│   ├── commands/              # NEW DIRECTORY
│   │   ├── cmd_system.c       # help, version, stats, reboot, factory-reset
│   │   ├── cmd_config.c       # show, set, save
│   │   ├── cmd_logging.c      # log, debug, diag
│   │   ├── cmd_diagnostics.c  # gpio, test
│   │   ├── cmd_decoder.c      # decoder
│   │   └── cmd_text.c         # send, mem, m1-m8, abort, pause, resume
│   └── commands.c             # DELETED (logic moved to commands/)
└── include/
    └── console.h              # Public API (unchanged)
```

**New Public API** (in command_registry.h):
```c
/* Subcommand dispatcher */
typedef struct {
    const char *name;
    console_error_t (*handler)(const console_parsed_cmd_t *parent);
} subcommand_descriptor_t;

console_error_t dispatch_subcommands(
    const subcommand_descriptor_t *subcmds,
    size_t count,
    const console_parsed_cmd_t *cmd,
    console_error_t (*default_handler)(const console_parsed_cmd_t *cmd)
);
```

---

## Command-to-Category Mapping

| Category | Commands | Total Lines |
|----------|----------|-------------|
| **System** | help, ?, version, v, stats, reboot, factory-reset, save | 200 |
| **Config** | show, set | 130 |
| **Logging** | log, debug, diag | 170 |
| **Diagnostics** | gpio, test | 220 |
| **Decoder** | decoder | 110 |
| **Text** | send, m1-m8, mem, abort, pause, resume | 190 |

**Total Handler Lines**: ~1020 (excluding registration boilerplate)

---

## Completion Cross-References

Commands with custom completion logic in `completion.c`:

| Command | Completion Type | Implementation |
|---------|-----------------|----------------|
| set, show | COMPLETE_PARAM | get_matching_params (lines 100-155) |
| diag | COMPLETE_DIAG | get_matching_diag_args (lines 160-171) |
| debug | COMPLETE_DEBUG | get_matching_debug_args (lines 210-238) |
| mem | COMPLETE_MEM_SLOT/SUBCMD | get_matching_mem_slots/subcmds (lines 176-203) |

**Refactoring Impact**: If commands are split into multiple files, completion logic must stay centralized (or use registration API)

---

## Next Steps

### Phase 1: Extract Subcommand Framework
1. Create `subcommand.c` with generic dispatcher
2. Refactor `cmd_stats` to use new dispatcher (proof-of-concept)
3. Verify tests pass

### Phase 2: Modularize by Category
1. Create `commands/` directory
2. Move system commands to `cmd_system.c`
3. Move config commands to `cmd_config.c`
4. Update CMakeLists.txt SRCS

### Phase 3: Eliminate Duplication
1. Generate m1-m8 commands with X-macro
2. Extract common helpers (confirm, slot validation)
3. Refactor large handlers (gpio, decoder)

### Phase 4: Testing
1. Add host tests for each command module
2. Test tab completion still works
3. Verify all 31 commands functional

---

## Open Questions

1. **Completion Sync**: How to keep completion.c in sync with split command files?
   - Option A: Commands register their completion handlers
   - Option B: Keep completion centralized, use comments to mark dependencies

2. **Command Registration**: Static array or dynamic registration?
   - Current: Static `s_commands[]` at compile time
   - Alternative: `CONSOLE_REGISTER_COMMAND(name, brief, handler)` macro

3. **Shared State**: Some commands access g_config directly. Extract to command context?

4. **Testing Strategy**: Mock printf, or capture output buffer?

---

## References

- Component: `/workspaces/RustRemoteCWKeyer/components/keyer_console/`
- Main file: `src/commands.c` (1296 lines)
- Command count: 31 (8 are m1-m8 aliases)
- Subcommand count: 21 (across 7 commands)
- Dependencies: 9 external components

---

## Handoff Metadata

**Prepared by**: Onboard Agent
**Date**: 2026-01-12
**Status**: Analysis Complete
**Next**: Implement subcommand framework (Phase 1)
**Blockers**: None
