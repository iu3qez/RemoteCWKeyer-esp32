# Task Completion Checklist

After completing any code change, verify:

1. **Compiles cleanly**: `idf.py build` with zero warnings (strict `-Werror`)
2. **Tests pass**: `cd test_host && cmake -B build && cmake --build build && ./build/test_runner`
3. **No RT violations**: No malloc, printf, ESP_LOGx, mutexes in Core 0 RT path
4. **Stream-only communication**: No callbacks, no shared state beyond KeyingStream
5. **Config changes**: If `parameters.yaml` changed, regenerate: `python scripts/gen_config_c.py parameters.yaml components/keyer_config`
6. **FAULT semantics preserved**: Corrupted timing → FAULT, never silent corruption
7. **PVS-Studio clean** (before commit): Run static analysis, fix all warnings
