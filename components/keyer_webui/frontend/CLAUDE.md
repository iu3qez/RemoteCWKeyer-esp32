<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_webui/frontend — Svelte single-page web UI (npm/Vite)

Responsibility: Owns the browser-side user interface — the SPA served by keyer_webui. A
separate module with its own npm toolchain; it produces static assets (JS/CSS/HTML) that
get embedded into firmware. It holds no device state of its own beyond view state and
talks to the board only over the REST + WebSocket API.

Key abstractions:
- App.svelte — shell + client-side router (history.pushState / popstate) over pages/:
  Home, Config, System, Keyer, Decoder, Timeline. main.ts mounts the app.
- lib/api.ts — `ApiClient` (exported singleton `api`): typed REST wrappers over /api/*
  and a self-reconnecting WebSocket client for /ws (decoded, word, pattern, paddle,
  keying, gap events) with device→browser timestamp synchronization.
- lib/types.ts — shared API DTOs; lib/themes.ts + lib/stores/theme.ts — theming, theme
  is loaded from device config on mount.

Depends on: svelte ^5, vite ^5, @sveltejs/vite-plugin-svelte ^4, typescript ^5 (all
devDependencies — no runtime npm deps; output is self-contained static assets).

Used by: keyer_webui (its CMakeLists runs `npm install` + `npm run build`, then
embed_assets.py bundles frontend/dist into src/assets.c).

External deps of note: none at runtime — plain fetch/WebSocket, no UI framework CDN.

Conventions: Scripts — `dev` (vite dev server, proxies /api to http://192.168.4.1),
`build` (vite build → dist/), `preview`. vite.config.ts pins output to assets/app.js and
inlines nothing (assetsInlineLimit 0) so embed_assets can gzip each file. __GIT_HASH__
and __BUILD_TIME__ are injected as build-time defines.

Gotchas: Do NOT read or touch node_modules — it is gitignored and regenerated on build.
The built bundle is embedded into flash, so any UI change requires an idf.py build to
take effect on-device; running the vite dev server alone only updates the browser, not
the board. New pages must be wired into BOTH App.svelte's router/navItems and the
backend SPA_ROUTES list, or deep links 404.
<!-- END treecode (auto) -->
