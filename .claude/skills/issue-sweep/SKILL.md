---
name: issue-sweep
description: Check every open GitHub issue's resolution condition against the tree, close the ones that hold with evidence, flag narrowed or stale ones. Run before a session handoff, or whenever the user asks which issues are done.
argument-hint: "[issue numbers, or empty for all open]"
allowed-tools: Bash(gh issue *), Bash(git *), Bash(grep *), Bash(ls *), Read
---

# Issue sweep

An issue closes on **evidence**, never on the feeling that the work is done. This skill is the
evidence pass: for each open issue, find the condition that would resolve it and test that
condition against the tree, read-only.

## Procedure

1. **List.** `gh issue list --state open --json number,title,labels` (or only `$ARGUMENTS`).
2. **Read every body.** `gh issue view N --json body,comments`. Extract the resolution condition —
   the *What would make it right* paragraph, or the equivalent. If an issue has no testable
   condition, say so: that is a finding, not a pass.
3. **Test the condition against the tree.** Read-only: `grep`, `ls`, `git log -S`, `git show`.
   Name the exact evidence — `file:line`, a commit SHA, a file that now exists or no longer does.
   Do not infer from titles, from memory of the session, or from a handoff's claims.
4. **Report one table** before touching anything:

   | # | condition | evidence | verdict |
   |---|-----------|----------|---------|

   Verdicts: `closable` (condition true, evidence cited) · `open` (condition false) ·
   `narrowed` (part of the condition is now met or moot — the issue needs its body updated, not
   closing) · `untestable` (no condition to test).
5. **Ask once**, then act. Closing is visible on the tracker: present the `closable` set and get
   one yes for the batch. Then `gh issue close N --comment "<evidence>"` — the comment names
   the commit or `file:line` that satisfies the condition, so the closure is arbitrable later.
6. **Never close a `blocking` issue.** It holds a decision, not a code condition; only the
   maintainer closes it, by making the decision. Report it, do not touch it.
7. **`narrowed` and `untestable` are reported, not edited.** Rewriting an issue's scope is the
   maintainer's call.

Definition of done for a closure: the closing comment lets someone a year from now verify it
without this session's context.
