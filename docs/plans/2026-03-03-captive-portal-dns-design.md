# Captive Portal DNS for Provisioning

**Date**: 2026-03-03
**Status**: Approved

## Problem

During provisioning, the keyer creates AP "CWKeyer-Setup" but the user must
manually navigate to `http://192.168.4.1`. No DNS hijacking or HTTP redirect
exists, so phones/laptops don't auto-detect the captive portal.

## Solution

Add DNS hijacking + HTTP 302 redirect inside the provisioning component.

### DNS Server (`provisioning_dns.c`)

- FreeRTOS task, UDP socket on port 53
- Responds to all A-type queries with `192.168.4.1`
- `prov_dns_start()` / `prov_dns_stop()` called from `provisioning.c`
- Based on Esp32VirtualUART's `dns_server.c` reference implementation

### HTTP 302 Redirect (in `provisioning_http.c`)

- Catch-all handler: if `Host` header is not `192.168.4.1`, respond 302 to
  `http://192.168.4.1/`
- Triggers captive portal detection on iOS/Android/Windows/macOS

### Flow

```
Phone connects to "CWKeyer-Setup"
  -> DNS query (any domain) -> response: 192.168.4.1
  -> HTTP GET http://connectivitycheck.gstatic.com/generate_204
  -> Server sees Host != 192.168.4.1 -> 302 -> http://192.168.4.1/
  -> Captive portal popup opens with setup page
```

## Scope

- All changes inside `components/provisioning/`
- No new component, no new config parameter
- No impact on RT path or normal operation

## Also Done

- `CONFIG_LWIP_LOCAL_HOSTNAME="RemoteCwKeyer"` added to `sdkconfig.defaults`
  for DHCP hostname announcement in STA mode
