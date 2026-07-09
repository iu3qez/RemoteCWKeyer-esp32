<!-- BEGIN treecode (auto) — do not edit inside this block -->
# provisioning — first-time device provisioning via AP + web form

Responsibility: Handles first-boot setup of an unconfigured device. Brings up an
open AP ("CWKeyer-Setup"), serves an HTTP form for WiFi credentials, callsign,
and theme, writes them to NVS, and reboots. Also detects a paddle-hold factory
reset at boot. This runs INSTEAD OF normal keyer boot — never alongside it — and
owns nothing on the RT path.

Key abstractions:
- provisioning_is_needed(): true when NVS has no WiFi SSID.
- provisioning_check_factory_reset(dit_gpio, dah_gpio, hold_ms): clears the
  stored SSID if both paddles are held at boot.
- provisioning_start(): creates AP, runs the web server, saves config, reboots.
  Does NOT return.
- Internal split: provisioning_wifi.c (AP/netif), provisioning_http.c (server +
  handlers), provisioning_html.c (embedded form page).

Depends on: nvs_flash, esp_wifi, esp_netif, esp_http_server, driver,
esp_driver_gpio, esp_timer, keyer_led.
Used by: main/main.c only (gate at startup before normal boot).
External deps of note: esp_http_server for the setup UI; raw GPIO reads for the
paddle factory-reset check; keyer_led for status feedback.

Conventions: Deliberately self-contained and duplicates small bits of WiFi/NVS
logic for fault isolation — do NOT refactor it to share code with keyer_wifi.
The whole flow is one-shot and terminates in esp_restart(). Strict warnings
(-Werror, -Wconversion, -Wsign-conversion).

Gotchas:
- provisioning_start() never returns; the device reboots via a deferred
  reboot_task after the form is submitted and committed to NVS.
- NVS keys here (PROV_NVS_WIFI_SSID/PASS/EN, CALLSIGN, THEME) are the contract
  with the normal-boot config loader — keep them in sync.
- It reads paddle GPIOs directly, so pin wiring must match the keyer config.
<!-- END treecode (auto) -->
