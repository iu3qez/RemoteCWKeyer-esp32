# Research Report: WireGuard Implementation for ESP-IDF/ESP32

Generated: 2026-01-21

## Summary

WireGuard VPN can be implemented on ESP32-S3 using the **esp_wireguard** library by trombik, which provides ESP-IDF native integration with the lwIP network stack. The library is based on the smartalock/wireguard-lwip implementation and supports ESP32, ESP32-S2, ESP32-C3, and ESP8266. While ESP32-S3 is not explicitly listed, the library contains no chip-specific code and should work without modification. Memory overhead is estimated at 15-30KB heap for the tunnel plus cryptographic operations.

## Recommended Library

**Use trombik/esp_wireguard** - Available on ESP Component Registry:

```bash
idf.py add-dependency "trombik/esp_wireguard^0.9.0"
```

| Aspect | esp_wireguard |
|--------|---------------|
| Framework | ESP-IDF native |
| Memory | ~20KB heap |
| Crypto | Software (ChaCha20-Poly1305) |
| IPv6 | Untested |
| Multiple peers | No |
| Maintenance | Active |

## API Pattern

```c
#include <esp_wireguard.h>

// 1. Configure
wireguard_config_t wg_config = ESP_WIREGUARD_CONFIG_DEFAULT();
wg_config.private_key = "device_private_key_base64";
wg_config.public_key = "peer_public_key_base64";
wg_config.allowed_ip = "10.0.0.2";
wg_config.allowed_ip_mask = "255.255.255.0";
wg_config.endpoint = "vpn.example.com";
wg_config.port = 51820;
wg_config.persistent_keepalive = 25;

// 2. Initialize and connect
wireguard_ctx_t ctx = {0};
esp_wireguard_init(&wg_config, &ctx);
esp_wireguard_connect(&ctx);

// 3. Check status
esp_wireguardif_peer_is_up(&ctx);

// 4. Disconnect
esp_wireguard_disconnect(&ctx);
```

## Critical Requirements

### Time Synchronization (CRITICAL)

WireGuard handshake REQUIRES synchronized time. The library does NOT sync automatically.

```c
// MUST sync NTP BEFORE connecting
sntp_setoperatingmode(SNTP_OPMODE_POLL);
sntp_setservername(0, "pool.ntp.org");
sntp_init();
// Wait for sync...
esp_wireguard_connect(&ctx);
```

### Configuration Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| private_key | Base64 | Yes | Device WireGuard private key |
| public_key | Base64 | Yes | Peer WireGuard public key |
| endpoint | String | Yes | Peer address (host:port) |
| port | uint16_t | Yes | Peer UDP port (default: 51820) |
| allowed_ip | IPv4 | Yes | Local tunnel IP address |
| allowed_ip_mask | IPv4 | Yes | Tunnel netmask |
| persistent_keepalive | uint16_t | Required for NAT | Keepalive interval (25s recommended) |

### Stack Size Requirements

When using NACL X25519 implementation, both `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE` and `CONFIG_MAIN_TASK_STACK_SIZE` must be at least 5KB.

## Known Limitations

1. **NAT Traversal**: Devices behind NAT MUST set `persistent_keepalive = 25`
2. **Pre-shared Key Bug**: Issue #28 - PSK may prevent handshake completion. Avoid PSK.
3. **IPv6**: Alpha/untested. Stick to IPv4 only.
4. **Single Peer Only**: Library only supports one tunnel/peer
5. **WiFi Only**: No Ethernet support
6. **ESP32-S3**: Not explicitly listed but should work (no chip-specific code)

## sdkconfig.defaults

```
# WireGuard
CONFIG_WIREGUARD_ESP_NETIF=y
CONFIG_WIREGUARD_x25519_IMPLEMENTATION_DEFAULT=y

# Stack sizes (increase if using NACL)
CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=5120
CONFIG_MAIN_TASK_STACK_SIZE=5120

# SNTP (required for WireGuard)
CONFIG_LWIP_SNTP_MAX_SERVERS=3
```

## Memory Footprint

| Component | Estimated Memory |
|-----------|------------------|
| WireGuard tunnel state | ~5-10 KB |
| Crypto buffers | ~5-10 KB |
| X25519 key exchange | ~2-5 KB |
| lwIP netif overhead | ~3-5 KB |
| **Total** | **15-30 KB heap** |

## Sources

- https://github.com/trombik/esp_wireguard
- https://components.espressif.com/components/trombik/esp_wireguard
- https://github.com/smartalock/wireguard-lwip
- https://www.wireguard.com/protocol/
