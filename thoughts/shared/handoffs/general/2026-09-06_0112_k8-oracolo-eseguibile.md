---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-05T23:12:13Z"
title: "The K8 becomes an executable oracle, and the suite discovers it isn't anchored"
summary: "Unblocked #7/#8, fixed the invented CWNet handshake, the firmware links again, the K8 reference read, emulated in gpsim and measured; #32 holds the verified spec and the maintainer's decisions for the third squeeze_mode."
keywords: ["k8", "k1el", "gpsim", "iambic", "squeeze_mode", "issue-32", "oracolo-differenziale", "issue-sweep"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32"
resume_focus: "Implement the third squeeze_mode (level sampled at instants k·u, default) in TDD against K8 traces generated with gpsim, per #32."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "docs-unblock-u5-u6"
head: "34deae5"
---

# The K8 becomes an executable oracle

Session of September 5-6, 2026, resumed from the handoff `2026-09-05_2145_sessione-zero-parziale.md`. It did four different things in sequence; the decisions are all on the tracker, this file only says where to look and what's fragile.

## How it went, in order

1. **Unblocking.** The two `blocking` issues (#7 sequence/identical/tolerance, #8 provenance) are **closed with the maintainer's decision** in their respective closing comments. No `blocking` open. U5/U6 of the test-bench plan are unblocked; the plan stays `requirements-only` because of #19 (doc review never done).
2. **Process.** Merged: #22 (issues closed on evidence, `/issue-sweep` before the handoff, hook on both `ce-handoff` paths, `.claude/` brought into convention, rule «one skill family only: CE»; superpowers uninstalled at the user level). Open: **PR #34** (STRATEGY: a boundary forbids building, not remembering). Branch without a PR: `docs-unblock-u5-u6` (5 docs that still said «blocked»), `debt-iambic-config-vs-k8` (one line in `code-quality.md`: double default of `squeeze_mode` in `parameters.yaml:120/260`).
3. **Hardware.** The firmware **hadn't linked since May** - `gen_config_c.py` was writing `config_nvs.c` into `include/src/`; fix merged (#28), build verified on ESP-IDF **v6.0.2** (EIM: activate with `. ~/.espressif/tools/activate_idf_v6.0.2.sh`, **not** `export.sh`; never piped to `tail`, or the variables die in the subshell). Flashed: boot ok, WiFi ok. Console bugs → #30 (editor: echo and state decide separately), #31 (no way to see IP/status; decision: a banner with `stats` on connect).
4. **CWNet.** `CWNET_CMD_WELCOME = 0x00` was invented: the server confirms **by sending the CONNECT back with the permissions**. Fix merged (#27, closes #23): READY on the echo, permissions preserved, **refuses to key without TRANSMIT**. Discovered #25 (the server arbitrates *who holds the key*, announced via `TX_INFO 0x05`, which we discard) and #26 (LED: only free/not-free, low priority).
5. **Keyer - the big one.** Audit of the host suite: the project claims two references and the suite doesn't anchor to either; the K8 **did not exist in the repo**. Then `morse8.zip` arrived. From there: licence (redistribution permitted, **GPL-incompatible → fetch-not-vendor**), three readings of the assembly (analysis, adversarial review, three-way comparison), emulation in gpsim, three measured experiments. **All on #32.**

## Where the truth lives

- **`#32`** - read the comments **from the bottom up**: every later comment corrects the previous one, and the last one on each topic wins. In particular: the verified sampling spec (once per **dit-unit**, not per element; level, not edge; same type cleared *after* the last sample), the three-way comparison (five universals = «the iambic», the rest variants), the three measurements (`mark 84 566`, `space 84 382`, sidetone `1208` cycles), and the maintainer's decisions.
- **`STRATEGY.md:62-64`** - the metric: decisions exact, timing tolerant, tolerance written before the test.
- **`components/keyer_iambic/src/iambic.c:200-232`** - the spot to change: today it detects an *edge* (`dit_is_fresh`, `:224`) inside a percentage window, in both modes. `can_arm_dit/dah` (`:207-208`) already does the opposite-only memory.
- **`test_host/test_iambic.c:88`** - the empty assertion (`SEND_DIT || SEND_DAH`) to replace.

## Maintainer decisions, all on #32

- **WPM computed** (PARIS), not copied from the K8; K8 durations only as fixtures in the tests, at equivalent speed (TIMEBASE 70 ≈ 14.2 WPM). The «5.7% slow» is closed for the product.
- **Third `squeeze_mode`, default**: level at instants k·u anchored to the element boundary + clears the same type after the last sample. Approved on condition the cost isn't excessive - estimated: a few dozen lines, replaces the window test with a unit-multiple crossing test. **Orthogonal to `IAMBIC_MODE_A/B`**, as in the K8: combining the K8's bonus element with the mode-dependent observation (DL4YHF) would give a fourth thing that is none of the three.
- **The percentage window stays** as an extension for high speeds, not as the default. Default = K1EL.
- **Opposite-only memory is the definition of iambic** (a natural debouncer) - not a K8 peculiarity. Already like that in the code.
- The K8 ships in **Mode B**; its A/B only changes release (B: one extra element; A: latch flush). It reads the level in **both** modes.
- **Nothing is blocked on DJ5IL**: `[Lit5]` is written as two cases with release at 0.8u (→E) and 1.5u (→A), read from the emulator. The article, once the maintainer has it, can change the label, not the test. The maintainer is gathering material (article and code) on his own.
- «Squeeze to K» test: from rest the K8 sends **DIT** (→ R). The test must say «dah, then close the dit».

## The work to do, and how

TDD against gpsim traces. Cycle: RED on `test_host` → GREEN in `iambic.c` → both CI variants (`cmake -B build` and `build-asan` with `-fsanitize=address,undefined`). Tests to write, in order: (1) level at k·u with a tap between two instants → lost; (2) `[Lit4]` N/T; (3) `[Lit5]` two cases 0.8u/1.5u; (4) `[Lit6/7]` K/C with «dah then dit»; (5) first element in a squeeze from rest = DIT. Residual to write into the tolerance: our tick is 1 ms, the level at k·u is read at the first tick ≥ k·u.

Then `tools/k8/` (archive.org pointer **with the snapshot timestamp**, sha256 `432df077…`, licence note + GPL conflict, `ref/` gitignored) - never vendor it.

## Machine-local, fragile

- `tmp/k8/` (gitignored): `morse8.asm` **original, do not touch** (sha256 `432df077a197…`), `morse8_gpasm.asm` (one single label `CONFIG→CONFIGM`, because gpasm reserves it), `morse8.hex/.cod`, `exp1-3*.stc/.log`, `parse_gpio_log.py`. Recipe: `gpasm --mpasm-compatible -p p12c509 -o morse8.hex morse8_gpasm.asm`; `gpsim -i -p pic12c509 -c exp.stc morse8.hex`; pins: gpio0 DIT, gpio1 DAH, gpio2 KEY, gpio3 PB, gpio4 TONE; inputs with pull-up, pressed = 0; power-on sign-on lasts until cycle ~591,556 with TX squelched - press after that.
- `morse8.zip` is **in the repo root, not gitignored**: a `git add -A` would commit it. Move it or ignore it.
- `tmp/oracle/` (gitignored): the 32 DL4YHF captures, copied out of `/tmp`, which evaporates.
- Full DL4YHF source and compiled `cwnet_dump`: `/private/tmp/claude-501/-Users-sf-Developer-RemoteCWKeyer-esp32/bed2bd1e-*/scratchpad/{dl4yhf,oracle}/` - **evaporates on restart**; refetch in `tools/cwnet/README.md`.
- Third comparison source: `/Users/sf/Developer/deskhpsdr/src/iambic.c` (GPL-3), outside the repo.
- `gputils 1.5.2` and `gpsim 0.32.1` installed via brew this session.

## Not done, and why

- No `squeeze_mode` line written: the session stopped at verified spec + decisions, at half a full context. It's the right point to restart clean.
- `[Lit5]` not written for the same reason, not because it's blocked.
- `tools/k8/` not created.
- The March PRs (#2, #3) remain parked, untouched.

## Verification done

Host suite 191/191 green, plain and ASan/UBSan, after #27. Firmware: `idf.py build` ok, `keyer_c.bin` 0x12a460, flashed, boot and WiFi ok (maintainer). Emulator: three experiments, every number within the predicted band or explained by the boundary path (~102 cycles). Issue sweep run before this handoff: none closeable, #15 narrowed.

Useful skills: `ce-work` for #32, `issue-sweep` before the next handoff (the hook reminds on both paths - verified live).
