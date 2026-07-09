<!-- BEGIN treecode (auto) — do not edit inside this block -->
# test_host — Unity host test harness (runs on the dev machine, no hardware)

Responsibility: Compiles the platform-independent parts of the `keyer_*` components on
the host and exercises them with Unity, so keying/decoding/protocol logic can be tested
without an ESP32. It owns the CMake build that selects which component `src/*.c` files
are host-safe, the ESP-platform stubs, and the test suites. It must NOT test HAL/driver
code or anything needing real GPIO/I2S/NVS — those files are deliberately excluded.

Key abstractions:
- `CMakeLists.txt` — the whole harness: strict flags (`-Wall -Wextra -Werror
  -Wconversion` …), `HOST_TEST=1` / target defines, FetchContent-pulled Unity v2.5.2,
  explicit lists of component sources (core, iambic, audio, logging, decoder, cwnet,
  console parser only) plus the `test_*.c` suites, linked into one `test_runner`.
- `test_main.c` — Unity entry / `RUN_TEST_GROUP` driver.
- `test_*.c` — one suite per unit (stream, iambic, sidetone, fault, decoder,
  timing_classifier, morse_table, cwnet_*, console_parser, …).
- `stubs/` — thin ESP shims (`esp_err.h`, `esp_timer.h`, `esp_stubs.c/.h`) supplying just
  enough of the platform (error codes, `esp_timer_get_time`) to link on the host.

Depends on (build-time, via include_directories/source lists): keyer_core, keyer_iambic,
keyer_audio, keyer_logging, keyer_console (parser only), keyer_config headers,
keyer_decoder, keyer_cwnet. Plus Unity, fetched over the network at configure time.

Used by: developers and CI — run `cmake -B build && cmake --build build && ./build/test_runner`
(add `-DCMAKE_C_FLAGS="-fsanitize=address,undefined"` for sanitizers).

External deps of note: Unity (ThrowTheSwitch, pinned v2.5.2 via FetchContent — needs git +
network on first configure).

Conventions: Components are driven ONLY through the lock-free stream by feeding fake,
recorded, or synthesized `stream_sample_t` sequences. There are NO mocks — per the
project rule, "if you need a mock, the design is wrong; the stream is the only interface."
The `stubs/` are platform shims for linking, not behavioral mocks of modules under test.

Gotchas:
- Adding a new host test or a newly-host-safe component source means editing the source
  lists in `CMakeLists.txt` by hand — there is no glob; commented-out entries mark files
  intentionally excluded because they pull in HAL/console/NVS dependencies.
- keyer_config sources are excluded (need NVS stubs); only its generated headers are on the
  include path, so a build must have generated them first.
<!-- END treecode (auto) -->
