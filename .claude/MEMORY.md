# RemoteCWKeyer-esp32 - Memory Index

## Strategy (2026-09-08)
[STRATEGY.md](../STRATEGY.md) is the anchor: every behaviour that matters is proven
against a real reference (CWNet protocol → DL4YHF client/server; keyer feel
→ the executed K1EL K8), not "made similar". Both ends are ours: the box on the
operator side, and `cwnetd`, a station daemon on a Linux or Mac PC, on the station side.

Active tracks: **Test bench**, **CWNet, both ends**, **Keyer** (logic in the
`Esp32KeyerTest` submodule), **Frictionless operating position**. Before proposing work, read the Boundaries:
WireGuard can be dropped, presets other than the K8 are unvalidated best effort,
no OTA for now, WebUI frozen until the first three tracks hold.

Test phrase: resist a change when the only argument is "it is there and costs little
to add", or when it cannot be proven against the reference.

## Blocking issues
A GitHub issue labelled `blocking` stops the work that depends on it, full stop. You do not plan
around it, you do not replace it with an assumption, you do not proceed by marking the work "provisional".
Full rule in [CLAUDE.md](../CLAUDE.md#a-blocking-issue-blocks), among the Critical Constraints.

No `blocking` issue open since 2026-09-06: #7 (known sequence, byte-for-byte equality,
zero tolerance) and #8 (session manifest with all fields) are closed with a decision
by the maintainer. U5 and U6 of the CWNet bench are unblocked.

## Definition of done
See [CLAUDE.md](../CLAUDE.md#definition-of-done). In short: host tests green in
both CI variants (plain + ASan/UBSan), never skip a test to get there,
and whoever touches `keyer_cwnet/` or `keyer_iambic/` brings a test against the reference.

## Feature Status (2026-05-07)
See [feature-status.md](feature-status.md) for full categorized feature checklist.

## Code Quality Notes
See [code-quality.md](code-quality.md) for stale code, dead stubs, and cleanup items.
