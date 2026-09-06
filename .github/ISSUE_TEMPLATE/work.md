---
name: Work
about: Something in the tree is wrong or missing, and the condition that fixes it can be tested against the tree.
title: ""
labels: []
---

<!--
Remove every comment block before filing. Keep the four headings verbatim:
/issue-sweep looks for them.

READER    Whoever decides what to do next, not the session doing the work.
          Analysis, reading notes and worked reasoning go in a file in the
          repo (docs/solutions/, a module CLAUDE.md, a handoff); the issue
          links it.
LENGTH    Body under 300 words. If it does not fit, it is two issues, or the
          analysis belongs in a file.
ONE       One problem per issue. A second problem found on the way is a
          second issue, or a light fix done on the spot (CLAUDE.md, "Work
          That Needs Deciding Becomes an Issue").
REFS      `file:line` only under "What would make it right" and toward frozen
          references (morse8.asm, DL4YHF traces). In prose about our own
          code name the function: line numbers rot at the next commit.
TITLE     What is wrong, as observed. Not the fix, not the plan.
NO        Narrating which rule this issue honours. "Nobody had noticed."
          Prose whose reader is its author.

COMMENTS  State changes and decisions only, under 150 words each. A longer
          analysis goes in a file; the comment carries the link and one line
          of outcome. A decision that supersedes an earlier one edits the
          body, never appends "this supersedes the above": a thread that must
          be read bottom-up is a log, not an issue.
CLOSING   On evidence: a comment naming the commit or `file:line` where the
          condition below holds. Not when the work feels done, not when the
          session ends.
-->

## What is wrong

<!-- What happens and what should happen, in two to four sentences. No
history of how it was found. -->

## Evidence

<!-- What shows it: a failing test, a trace, a measurement, a reference that
says otherwise. Cite; do not reproduce. -->

## What would make it right

<!-- One condition, testable against the tree by someone with no memory of
this session. If a decision has to be taken first, file a Decision issue and
name it under "Blocked on". -->

## Blocked on

<!-- Issue numbers, or "nothing". A `blocking` issue here stops this work
completely (CLAUDE.md, "A Blocking Issue Blocks"). -->
