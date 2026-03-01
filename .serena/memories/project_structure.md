# Project Structure

```
RemoteCWKeyer-esp32/
├── CMakeLists.txt              # Top-level ESP-IDF project
├── sdkconfig.defaults          # ESP-IDF configuration
├── partitions.csv              # Flash partition table
├── parameters.yaml             # Config source of truth
├── ARCHITECTURE.md             # Immutable design principles
├── CODING_STYLE.md             # C coding patterns
├── CLAUDE.md                   # AI assistant instructions
├── components/
│   ├── keyer_core/             # Stream, sample, consumer, fault
│   ├── keyer_iambic/           # Iambic FSM (pure logic)
│   ├── keyer_audio/            # Sidetone, buffer, PTT
│   ├── keyer_logging/          # RT-safe logging (rt_log.h)
│   ├── keyer_console/          # Serial console
│   ├── keyer_config/           # Generated config (DO NOT EDIT)
│   ├── keyer_webui/            # Web UI (frontend/ has npm)
│   ├── keyer_hal/              # GPIO, I2S, ES8311 HAL
│   ├── keyer_cwnet/            # CWNet remote keying protocol
│   ├── keyer_decoder/          # Morse decoder
│   ├── keyer_wifi/             # WiFi management
│   ├── keyer_vpn/              # VPN
│   ├── keyer_led/              # LED control
│   ├── keyer_text/             # Text/morse table
│   ├── keyer_usb/              # USB support
│   ├── provisioning/           # Device provisioning
│   └── src/                    # (additional sources)
├── main/
│   ├── main.c                  # Entry point
│   ├── rt_task.c               # Core 0 RT task
│   ├── bg_task.c               # Core 1 background task
│   └── audio_test.c            # Audio testing
├── scripts/
│   ├── gen_config_c.py         # Config code generator
│   └── gen_log_tags.py         # Log tag generator
├── test_host/                  # Unity-based host tests
│   ├── CMakeLists.txt
│   ├── test_main.c             # Test runner entry
│   ├── test_stream.c           # Stream tests
│   ├── test_iambic.c           # Iambic FSM tests
│   ├── test_sidetone.c         # Sidetone tests
│   ├── test_fault.c            # Fault system tests
│   ├── test_decoder.c          # Decoder tests
│   ├── test_cwnet_*.c          # CWNet protocol tests
│   ├── test_console_parser.c   # Console tests
│   └── stubs/                  # Test stubs
├── docs/                       # Design documents
├── tools/                      # Development tools
└── thoughts/                   # Design thinking notes
```

## Key Entry Points
- `main/main.c` — Application entry, initializes all subsystems
- `main/rt_task.c` — Hard real-time loop on Core 0
- `main/bg_task.c` — Background tasks on Core 1
