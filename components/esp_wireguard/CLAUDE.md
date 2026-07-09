<!-- BEGIN treecode (auto) — do not edit inside this block -->
# esp_wireguard — vendored WireGuard implementation (in-tree fork)

Responsibility: Provides the WireGuard protocol (Noise handshake, ChaCha20-
Poly1305, Curve25519/BLAKE2s crypto) and an lwIP netif that tunnels IP traffic.
Owns the low-level tunnel; keyer_vpn is the only in-tree consumer and drives it
through the public API. This is third-party code — keep changes minimal and
patch-documented.

Key abstractions:
- wireguard_config_t: private/public keys, endpoint, port, allowed IP + mask,
  keepalive (see ESP_WIREGUARD_CONFIG_DEFAULT).
- wireguard_ctx_t: config + lwIP netif + saved default netif.
- esp_wireguard_init / esp_wireguard_connect / esp_wireguard_set_default /
  esp_wireguardif_peer_is_up / esp_wireguard_disconnect.

Depends on: esp_netif, lwip, mbedtls (REQUIRES).
Used by: keyer_vpn (only). Not called directly from main or any other component.
External deps of note: bundles its own reference crypto (crypto/refc, nacl
curve25519) rather than relying on mbedtls for the primitives.

Conventions: Treat as vendored/frozen. Warning suppressions are applied
per-file in CMakeLists.txt, not globally: src/crypto/refc/x25519.c gets
-Wno-error=stringop-overread, and src/wireguard.c gets
-Wno-error=unterminated-string-initialization (GCC 15 flags the protocol's
fixed byte-string constants CONSTRUCTION/IDENTIFIER/LABEL_MAC1/LABEL_COOKIE).

Gotchas:
- DO NOT REPLACE with the component-registry version of trombik/esp_wireguard.
  This is an in-tree fork of 0.9.0; upstream is ESP-IDF v5-only and will not
  build on v6.
- RNG patch: mbedtls 4 / TF-PSA-Crypto split removed mbedtls_entropy_* and
  mbedtls_ctr_drbg_* linkage, so the fork was patched to call esp_fill_random()
  directly (ESP32 HW RNG — cryptographically strong once WiFi/RF is up). Do not
  "restore" the mbedtls entropy path.
- Bring the tunnel up only after WiFi is connected and time is synced (keyer_vpn
  enforces this); the handshake depends on a correct clock.
<!-- END treecode (auto) -->
