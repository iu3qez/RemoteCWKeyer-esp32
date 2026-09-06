<!--
Remove the comment blocks before filing. Keep the headings. Under a heading
that does not apply write "n/a" and nothing else.

READER    The maintainer, once, at review. Say what is now different and why
          it was worth doing. Do not restate the diff: the Files tab does.
EVIDENCE  The Definition of done (CLAUDE.md) is answered with evidence, not
          checkmarks: a number, a test name, a `file:line`. A line that
          cannot be filled honestly is a PR that is not done.
NO        Narrating which rule this PR honours. Model or harness signatures
          (CLAUDE.md, "Git conventions").
-->

## What changes

<!-- One or two sentences a reviewer can stop after. Then only what the diff
does not show: the decision taken, the alternative rejected. -->

## Closes

<!-- Issue #N, the condition under its "What would make it right", and where
it now holds: `file:line` or commit. Or "none". If only part of the
condition holds, say which part and that the issue stays open. -->

## Definition of done

- Host tests: <!-- N/N plain, N/N ASan/UBSan. n/a for a docs-only change. -->
- Reference test: <!-- keyer_cwnet or keyer_iambic only: the test function, and the reference it pins against. Otherwise n/a. -->
- RT path: <!-- untouched, or what changed on Core 0 and why it stays inside the 100 µs ceiling. -->
- Blocking issues open on this work: <!-- none. If not none, this PR should not exist yet. -->

## Not done here

<!-- Deliberately deferred scope, stated once, with where it is tracked. Or
"nothing". -->
