#!/usr/bin/env python3
"""Remind to run /issue-sweep before a handoff is created.

Wired to both paths that create one (see .claude/settings.json):
  UserPromptExpansion, matcher ce-handoff  -- the user typed /ce-handoff
  PreToolUse,          matcher Skill       -- Claude invoked ce-handoff itself
Reminds, never blocks: a "sweep already done" state would go stale within minutes.
Reads the hook JSON on stdin; prints plain text (expansion) or additionalContext JSON (PreToolUse).
"""
import json
import sys

MSG = ("issue-sweep: a handoff closes a session. Before creating it, run /issue-sweep: "
       "check every open issue's resolution condition against the tree, close the ones that "
       "hold (with evidence), flag the ones whose scope narrowed. Skip only if the sweep "
       "already ran this session after the last change to the tree. See CLAUDE.md, "
       "'An issue closes on evidence'.")

h = json.load(sys.stdin)
ev = h.get("hook_event_name", "")
if ev == "UserPromptExpansion":
    if "ce-handoff" not in h.get("command_name", ""):
        sys.exit(0)
    args = h.get("command_args", "") or ""
elif ev == "PreToolUse":
    ti = h.get("tool_input", {})
    if "ce-handoff" not in ti.get("skill", ""):
        sys.exit(0)
    args = ti.get("args", "") or ""
else:
    sys.exit(0)

if args.strip().startswith("resume"):   # resuming is not closing a session
    sys.exit(0)

if ev == "PreToolUse":
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse", "additionalContext": MSG}}))
else:
    print(MSG)
