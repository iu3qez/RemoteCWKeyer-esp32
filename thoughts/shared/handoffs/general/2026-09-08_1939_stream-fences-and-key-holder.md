---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-08T19:39:33Z"
title: "Server-side key, stream seqlock, config re-read: three PRs merged, three issues closed, CI on arm64"
summary: "Session September 8, 2026 (afternoon): #25 (TX_INFO, who holds the key), #57 (stream publication order, two fences), #69 (config re-read) merged as #66, #70, #71; the arm64 leg in CI that failed a half-fix; sweep done, #26 narrowed and the station side (#64, #65, #68) from the other handoff of the day remain."
keywords: ["cwnet", "tx_info", "key-holder", "callsign", "stream", "seqlock", "fence", "acquire", "release", "arm64", "rt_task", "config", "issue-25", "issue-57", "issue-69", "issue-26", "issue-64", "issue-68", "issue-65"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/remotecwkeyer-issue-review-8ea3ab"
resume_focus: "The station side from the 2026-09-08_1000 handoff (#64 daemon, #68 host client; GUI on hold for #65 blocking). In queue: #26 narrowed (binary LED already decided by the maintainer, name in the Web UI), and a docs/solutions entry on the seqlock's two fences, which the corpus doesn't have."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "ca321ff"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/remotecwkeyer-issue-review-8ea3ab"
---

# Server-side key, stream seqlock, config re-read

Resumed from `2026-09-08_0912_cwnet-client-track.md`. In parallel, another session the
same day flipped the strategy on the station side and wrote
`2026-09-08_1000_station-daemon-start.md`: that's the handoff for #64, #65, #68; this
one covers the client and the core. Everything is on `main` at `ca321ff`. Three PRs
merged by the maintainer, CI green on ten legs: **#66** (#25), **#70** (#57), **#71**
(#69). Host suite from 205 to 218. Nothing proven on hardware.

## What exists now, and where

- **Who holds the key** (#25, PR #66). `cwnet_client.c`: `handle_tx_info()` takes the
  `TX_INFO 0x05` frame (signed index + NUL-terminated callsign) and derives
  `cwnet_key_holder_t`: UNKNOWN / FREE / MINE / OTHER, exactly as announced, with no
  timer of our own. "Mine" = index ≥ 1 and callsign equal to the one sent in CONNECT,
  which now carries `g_config.system.callsign` (fallback username). Exposed by
  `cwnet_socket_get_key_holder()`, by `bg_task`'s periodic status line and by
  `key_holder` / `key_holder_name` in `/api/system`. Decision from the maintainer in the
  body of #25: the box keys anyway and signals it. Fixture: the three payloads from the
  September 5 capture in `test_host/cwnet_fixtures.h`.
- **Single-producer stream** (#57, PR #70). `stream.c`: release fence, sample store,
  `write_idx` publish with a release store; `behind >= capacity` is overrun (the slot
  `capacity` back is the producer's next one); `stream_read()` rechecks the index behind
  an acquire fence after the copy and discards; `consumer_resync()` lands at
  `capacity - 1`. ARCHITECTURE.md 2.1.4, 3.1.2, 3.1.3, 3.2 rewritten, Amendment 003. The
  treecode block of `keyer_core/CLAUDE.md` is regenerated with the plugin's
  cartographer.
- **Config re-read** (#69, PR #71). `rt_task.c`: an acquire fence between copying the
  fields and the relaxed re-read of the generation. The comment says what the guard
  actually proves: the setter bumps *after* the store, so a mixed set survives one idle
  tick and `last_config_gen` (the pre-read value) makes it reload on the next one. Also
  removed a one-shot `ESP_LOGI` on Core 0 in `hal_audio_write()`.
- **CI**. `host-tests.yml` runs the plain / asan-ubsan matrix on `ubuntu-latest` and
  `ubuntu-24.04-arm`, `timeout-minutes: 20`. `name` is a matrix axis: with only `os` as
  an axis, the second `include` was overwriting the first and the plain legs vanished.

## What the session taught

- **A seqlock has two halves, and needs two fences.** Reader: an acquire load doesn't
  order the loads *before* it; between the copy and the re-read you need
  `atomic_thread_fence(acquire)` (3-6 samples torn under ASan on M1 without it). Writer:
  a release store doesn't order the stores *after* it; between the previous publish and
  the slot's bytes you need `atomic_thread_fence(release)` (1 and 80 samples torn on
  CI's plain arm64 leg without it; never seen on M1 or x86). The test:
  `test_stream_two_threads_never_accept_a_stale_or_torn_sample`, every sample carries
  its own index. The `docs/solutions/` corpus has nothing on the memory model: a
  candidate for an entry (`ce-compound`).
- **The arm64 leg failed a fix that always passed locally.** Doubling up on jobs pays
  off.
- **The name in `TX_INFO` is the callsign, not the username** (`CwNet.c:437`); at login
  the server only checks the username (`CwNet_CheckUserAndGetPermissions`).
- **Review**: the mutation done by the testing reviewer (isolated worktree) found the
  unbounded stress loop and the unpinned `index >= 1` guard; the adversarial review on
  Opus with a mandate to falsify against the source remains the step that pays off.

## Sweep of the evening of 2026-09-08

Closed on evidence: #25, #57, #69. **#26 narrowed**: point 1 (client exposes state and holder) holds; what's left is the LEDs (binary free / not free, decided by the maintainer on September 5) and the Web UI with the name. Open and unchanged, issues #54, #48, #46, #33, #19, #17; #64 and #68 (station side, other handoff); #65 `blocking` for the daemon's GUI.

Two P2 findings from the #25 review (twin callsign read as "mine"; username on the wire
with no callsign) are **wontfix from the maintainer**, logged in memory: don't
re-propose them.

## Pattern and vocabulary

The maintainer wants **manipolare / manipolazione** (to key / keying); block 0x06 is
«la stringa di controllo radio» (the radio control string). Adversarial review on Opus
before the PR, sonnet for the rest; the model must always be stated.

## Machine-local state, fragile

- DL4YHF source: `~/Downloads/Remote_CW_Keyer_Sources.zip` (sha256 `d960d6b9...`),
  extracted into the session scratchpad (evaporates). CRLF files: `grep -a`,
  `tr -d '\r'`.
- Captures in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/` (session 12 =
  keying, 47 `TX_INFO`; session 10 = sysop).
- Artifacts from the three reviews in `/tmp/compound-engineering-501/ce-code-review/`.
- Light debt in `.claude/code-quality.md`: stale treecode blocks (`test_host`,
  `keyer_cwnet`), the silence-marker clamp at 65.5 s. From #70, not in an issue:
  `consumer_resync()` gets overtaken again on the next push (no production caller); the
  torn-read `continue` in `rt_task` also skips the tick delay.
