<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_config — GENERATED configuration component (DO NOT EDIT)

>>> DO NOT EDIT ANY FILE IN THIS COMPONENT BY HAND. <<<
Every .c and .h here is machine-generated at build time from
`/parameters.yaml` by `/scripts/gen_config_c.py`. Hand edits are silently
overwritten on the next build. Only `CMakeLists.txt` is checked into git;
the `src/` and `include/` trees do not exist until the first build generates them.

Responsibility: Provides the process-wide configuration model: the atomic `g_config`
struct, its default initializer, NVS load/save persistence, and the console command
surface for reading/writing parameters. It owns the single source of runtime settings
(WPM, iambic/memory/squeeze modes, sidetone freq/volume, GPIO pins, WiFi/VPN/LED
params, etc.) and the `generation` counter used for lock-free hot-reload.

Key abstractions (all generated):
- `config.h` / `config.c` — `keyer_config_t g_config`, `config_init_defaults()`,
  and the `CONFIG_GET_*()` accessor macros used everywhere else.
- `config_nvs.h` / `config_nvs.c` — `config_load_from_nvs()` / save to NVS.
- `config_console.h` / `config_console.c` — console get/set command handlers.
- `config_meta.h`, `config_schema.h` — parameter metadata / schema tables.

Depends on: `nvs_flash` (only REQUIRES entry).

Used by: nearly everything — main, rt_task, bg_task, and most `keyer_*` components read
`g_config` through the generated macros.

External deps of note: ESP-IDF NVS for persistence; Python3 + PyYAML run at configure
time by the CMake `add_custom_command` (build fails hard via `COMMAND_ERROR_IS_FATAL`
if generation fails, rather than using stale files).

Conventions: To change a parameter, edit `/parameters.yaml` and rebuild — or regenerate
manually with `python scripts/gen_config_c.py parameters.yaml components/keyer_config`.
Config values are accessed atomically; cross-core reads use the `generation` counter to
detect torn reads.

Gotchas:
- The component looks empty in git (just CMakeLists.txt) — that is expected, not a bug.
- Because sources are generated, grepping this dir before a build finds nothing; read
  `parameters.yaml` and the generator to understand what fields exist.
- Adding/renaming a field means updating `parameters.yaml` only; never patch the .c/.h.
<!-- END treecode (auto) -->
