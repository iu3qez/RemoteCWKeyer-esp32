#!/bin/sh
# PreToolUse Edit|Write: refuse edits to files generated from parameters.yaml.
# Reads the hook JSON on stdin (tool_input.file_path). Exit 2 = block, stderr shown to Claude.
FILE=$(python3 -c 'import sys,json; print(json.load(sys.stdin).get("tool_input",{}).get("file_path",""))' 2>/dev/null) || exit 0
case "$FILE" in
  */keyer_config/include/config.h|*/keyer_config/include/config_meta.h|*/keyer_config/include/config_nvs.h|\
  */keyer_config/src/config_nvs.c|*/keyer_config/include/config_console.h|*/keyer_config/include/config_schema.h)
    echo "BLOCKED: $FILE is generated from parameters.yaml. Edit parameters.yaml; idf.py build regenerates it (or: python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include)." >&2
    exit 2;;
esac
exit 0
