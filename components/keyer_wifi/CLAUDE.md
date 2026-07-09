<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_wifi — WiFi bring-up (STA with AP fallback)

Responsibility: Owns the device's WiFi lifecycle: init the esp_wifi/esp_netif
stack, attempt an STA connection to the configured SSID, and fall back to an
open AP if that fails. Publishes a single atomic state value for other Core 1
consumers (LED, web UI, console, VPN). It must NOT do any provisioning UI
(that is the isolated `provisioning` component) and must never run on the RT
path.

Key abstractions:
- wifi_config_app_t / WIFI_CONFIG_DEFAULT: SSID, password, timeout, and optional
  static-IP settings.
- wifi_state_t: DISABLED / CONNECTING / CONNECTED / FAILED / AP_MODE.
- wifi_app_init / wifi_app_start / wifi_app_stop: non-blocking lifecycle.
- wifi_get_state / wifi_get_ip / wifi_is_connected: thread-safe status reads.

Depends on: esp_wifi, esp_netif, esp_event, nvs_flash.
Used by: main/main.c and main/bg_task.c (startup + LED mapping), keyer_webui
  (api_system.c), keyer_console (commands.c). keyer_vpn waits on
  wifi_is_connected() before starting its tunnel.

External deps of note: esp_event default loop and IP/WIFI event handlers drive
the STA reconnect logic; STA and AP netifs must be created before
esp_wifi_init() (ESP-IDF ordering requirement).

Conventions: State is exposed only through the atomic accessor functions — no
shared structs. Compiled with -Werror plus -Wconversion/-Wshadow.

Gotchas:
- esp_event_loop_create_default() may already exist; ESP_ERR_INVALID_STATE is
  treated as benign.
- On STA failure the module switches to open AP mode rather than staying down,
  so "connected" from a client's view can mean AP_MODE — check wifi_get_state()
  not just is_connected().
- Non-blocking start: callers must poll wifi_get_state(), not assume readiness.
<!-- END treecode (auto) -->
