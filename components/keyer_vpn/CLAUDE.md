<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_vpn — VPN glue (WireGuard tunnel manager)

Responsibility: Thin orchestration layer that brings up a WireGuard tunnel using
the vendored esp_wireguard component. Owns the connect sequence — wait for WiFi,
sync wall-clock time via SNTP (required for the WireGuard handshake), build the
wireguard_config_t, init/connect, then monitor. Publishes an atomic vpn_state_t
and tunnel stats. It must NOT run on the RT path; the whole thing lives on Core 1
and is best-effort with zero impact on keying.

Key abstractions:
- vpn_config_app_t / VPN_CONFIG_DEFAULT: endpoint, port (51820), server public
  key, client private key, tunnel address/CIDR, allowed_ips, keepalive.
- vpn_state_t: DISABLED / WAITING_WIFI / WAITING_TIME / CONNECTING / CONNECTED /
  FAILED.
- vpn_stats_t: tx/rx bytes, handshake count, last handshake time.
- vpn_app_init / vpn_app_start (spawns a pinned Core 1 task) / vpn_app_stop,
  plus vpn_get_state / vpn_is_connected / vpn_get_stats / vpn_state_str.

Depends on: esp_netif, esp_event, nvs_flash, esp_timer, keyer_logging,
keyer_config, keyer_wifi (REQUIRES); esp_wireguard (PRIV_REQUIRES).
Used by: main/main.c and main/bg_task.c (startup + LED status), keyer_webui
  (api_vpn.c), keyer_console (commands.c).

External deps of note: esp_sntp (pool.ntp.org / time.google.com) — the tunnel
cannot come up until time is synced, because WireGuard timestamps the handshake.
Wraps esp_wireguard_init/connect/set_default and esp_wireguardif_peer_is_up.

Conventions: State transitions use explicit atomic stores with acquire/release
ordering; the worker task cooperatively exits when state is set to DISABLED.
Strict warning set (-Werror, -Wconversion, -Wsign-conversion, stack protector).

Gotchas:
- Requires a working clock; a device with no SNTP reachability stalls in
  WAITING_TIME and never connects.
- esp_wireguard_init must not be called twice — disconnect before reconfiguring.
- Keepalive 25 is the NAT-friendly default; 0 disables it.
<!-- END treecode (auto) -->
