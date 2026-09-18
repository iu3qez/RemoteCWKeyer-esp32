---
artifact_contract: "ce-handoff/v1"
created_at: "2026-09-09T20:54:10Z"
title: "The colour rule, the verdict on the key, the LED pruning"
summary: "Session September 9, 2026: #73 (docs/solutions entry on the two-fence seqlock) and #74 (nine LED states become five situations under the rule green = your CW gets out, the can_transmit verdict, the KEY row in the Web UI) merged, #26 closed on evidence; sweep with no other closures; next step the station side (#64, #68) with the GUI on hold for #65."
keywords: ["led", "led_render", "colour-rule", "on-air", "off-air", "can_transmit", "key-holder", "tx_info", "seqlock", "fence", "docs-solutions", "concepts", "issue-26", "issue-64", "issue-65", "issue-68", "station-daemon", "esp-idf", "eim"]
cwd: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
resume_focus: "The station side: #64 daemon and #68 host client, restarting from 2026-09-08_1000_station-daemon-start.md and from the uncommitted work in the station-daemon-startup-03165a worktree (the daemon plan from September 8). The GUI stays on hold for #65 blocking."
repository: "iu3qez/RemoteCWKeyer-esp32"
repo_root_sha: "f153e01ec202b2cae17102fa0f355d657bb641c7"
branch: "main"
head: "75dfb28"
worktree_path: "/Users/sf/Developer/RemoteCWKeyer-esp32/.claude/worktrees/busy-chaum-c78815"
---

# The colour rule, the verdict on the key, the LED pruning

Resumed from `2026-09-08_1939_stream-fences-and-key-holder.md`, which left a
`docs/solutions/` entry on the two fences and #26 narrowed in the queue. Both are
closed. Everything is on `main` at `75dfb28`, two PRs merged by the maintainer, CI green
on ten legs including `firmware-build`. Host suite from 218 to 228. Nothing proven on
hardware: nobody has seen the new LEDs lit yet.

## What exists now, and where

- **A seqlock has two halves** (PR #73).
  `docs/solutions/architecture-patterns/a-seqlock-has-two-halves-and-needs-two-fences.md`:
  what an acquire load and a release store *don't* order, the torn counts per platform,
  the test that makes the tear observable. `CONCEPTS.md` now has the entry *Keying
  stream* (the in-RAM ring) distinct from *MORSE keying stream* (the CWNet wire), with
  the ambiguity noted at the end. `CODING_STYLE.md`, memory-order guide: the tags order
  one side only, the other side needs the fence.
- **The colour rule** (PR #74, #26 closed). To be read, not rewritten: the body of #26
  (the maintainer's decision of 2026-09-09, which supersedes the narrowing from the
  5th) and the header of `components/keyer_led/include/led_render.h`. Nine states are
  five situations, one per colour, in `led_situation_t`; the animations are events
  (`led_notify()`, three flashes in the colour they land on); the render is pure in
  `components/keyer_led/src/led_render.c`, `led.c` is just the RMT shell. The mapping
  from WiFi and CWNet to the situation is `main/bg_task.c:66-84`, `led_situation_now()`,
  reread on every tick. Eight tests in `test_host/test_led.c`; the one that matters is
  `test_led_green_belongs_to_on_air_and_to_nothing_else`.
- **The verdict on the key**. `cwnet_client_can_transmit()` in
  `components/keyer_cwnet/src/cwnet_client.c`: READY, TRANSMIT granted, key free or
  ours, i.e. what the reference server does with a MORSE frame (`CwNet.c:2875-2902`).
  UNKNOWN counts as "no": the announcement follows the CONNECT echo in the same burst
  (session 12 capture, offset 169). Wrapper `cwnet_socket_can_transmit()`, field
  `can_transmit` in `/api/system`, KEY row in `System.svelte:197-199` with `keyLabel()`
  at line 21. Tests pinned to the capture's fixtures in
  `test_host/test_cwnet_client.c`, the two `test_client_can_transmit_*`.

## Decisions, and whose they are

- **From the maintainer**: the rule «verde vuol dire che il tuo CW esce, ogni altro
  colore vuol dire che non esce e il colore dice perché» [green means your CW gets out,
  every other colour means it doesn't and the colour says why]; the animations are
  events; the paddle overlay stays because it's needed in debug. All in chat on
  2026-09-09, logged in the body of #26.
- **Mine, implicitly approved with the merge**: the overlay carries position and
  brightness in the situation's colour, never a colour of its own (a green overlay on a
  red base would contradict the rule while keying); the squeeze is the whole bar lit,
  magenta leaves the vocabulary; WiFi disabled in config is green, not yellow, because
  the box is the local keyer it was asked to be; the half-second red flash before the
  yellow is removed.
- **Discarded alternative**: reserving some of the seven LEDs for the link. It's the
  code that's there to remember the maintainer had already excluded it on September 5.

## What the session taught

- **The paddle overlay is a diagnostic tool, with a precise limit.** `bg_task` reads the
  pins on its own (`hal_gpio_read_paddles()`, raw `gpio_get_level`, upstream of the 5 ms
  debounce and the FSM). LED lit and CW silent: a fault downstream in software. LED off:
  the contact doesn't reach the pin. At 100 Hz it tells you *whether* the contact
  closes, not how clean it is: bounce is invisible.
- **A handoff that says "X isn't on this machine" needs better verification than that.**
  The previous one implied the firmware only builds in CI; that was false, and I
  repeated it in PR #74. Three negative clues (`which`, `$IDF_PATH`, `~/esp`) weren't
  enough.
- **A seqlock has two halves** is now in the corpus; the memory model is no longer a
  hole in `docs/solutions/`.

## Sweep of 2026-09-09

Closed on evidence: #26 (comment with `file:line` on `main`). Open, condition tested
false against the tree: #68, #64, #54, #48, #46, #33, #19, #17. #65 `blocking` for the
daemon's GUI, untouched. #74 was merged after the sweep and its diff doesn't touch any
of the files those eight conditions test.

## Machine-local state, fragile

- **ESP-IDF exists and is v6.0.2**, installed with eim in `~/.espressif/v6.0.2/`. It
  activates with `source ~/.espressif/tools/activate_idf_v6.0.2.sh`. The `export.sh`
  inside esp-idf **doesn't work** with this layout: it looks for a venv that eim doesn't
  create. It's in no zsh rc file.
- **Worktree `.claude/worktrees/station-daemon-startup-03165a`**, branch
  `claude/station-daemon-startup-03165a` at `2ac9689`, with three uncommitted files:
  `CONCEPTS.md`, `parameters.yaml` modified and
  `docs/plans/2026-09-08-2158-feat-station-daemon-plan.md` new. It's the station
  daemon's plan from September 8 and the first place to look for #64. It doesn't
  survive an absent-minded cleanup: today's worktree sweep left it alone on purpose.
- Captures in `/Users/sf/Developer/RemoteCWKeyer-esp32/tmp/oracle/`, not committed
  (session 12 = keying and `TX_INFO`; session 10 = sysop). DL4YHF source in
  `~/Downloads/Remote_CW_Keyer_Sources.zip`; CRLF files, `grep -a`.
- Two light debts from #70 and #71 under **To fix** in `.claude/code-quality.md`
  (lines 13-14): `consumer_resync()`'s re-overtake, the `continue` that skips the tick
  delay in the config re-read. The September 8 handoff treated them as logged and they
  weren't.
- 36 merged remote branches from earlier sessions are still on `origin`, waiting on a
  word from the maintainer. The local ones and the six stale worktrees were removed
  today.
- The treecode block of `components/keyer_led/CLAUDE.md` is regenerated with the
  cartographer; those for `test_host` and `keyer_cwnet` remain stale, already in
  `code-quality.md`.

## Pattern and vocabulary

Adversarial review on Opus before the PR, sonnet for the rest; the model must always be
stated. The maintainer wants **manipolare / manipolazione** (to key / keying). Choice
questions go through chat, not the blocking-question tool: in the desktop app the text
before the tool doesn't arrive.
