# Suggested Commands

## Environment Setup
```bash
source /home/sf/esp/esp-idf/export.sh
```

## Build
```bash
# Frontend deps (first time)
cd components/keyer_webui/frontend && npm install && cd -

# Full build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Code Generation
Config is generated from `parameters.yaml`. Never edit `keyer_config/` directly.
```bash
python scripts/gen_config_c.py parameters.yaml components/keyer_config
```

## Testing
```bash
cd test_host
cmake -B build && cmake --build build && ./build/test_runner

# With sanitizers
cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build && ./build/test_runner
```

## Static Analysis (PVS-Studio)
```bash
idf.py build  # generates compile_commands.json
pvs-studio-analyzer analyze -f build/compile_commands.json -o build/pvs-report.log -j4
plog-converter -t fullhtml -o build/pvs-report build/pvs-report.log
```

## Utility Commands
```bash
git status / git log --oneline
ls, find, grep  # standard Linux tools
```
