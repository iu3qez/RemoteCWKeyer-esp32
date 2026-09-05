#!/bin/sh
# PostToolUse Edit|Write: regenerate keyer_config after parameters.yaml changes.
FILE=$(python3 -c 'import sys,json; print(json.load(sys.stdin).get("tool_input",{}).get("file_path",""))' 2>/dev/null) || exit 0
case "$FILE" in
  */parameters.yaml)
    cd "${CLAUDE_PROJECT_DIR:-.}" && echo "Regenerating keyer_config from parameters.yaml..." \
      && python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include;;
esac
exit 0
