/* Auto-generated - DO NOT EDIT */
#ifndef CONFIG_SCHEMA_JSON_H
#define CONFIG_SCHEMA_JSON_H

static const char CONFIG_SCHEMA_JSON[] = R"JSON(
{
  "parameters": [
    {
      "name": "keyer.wpm",
      "type": "u16",
      "widget": "slider",
      "description": "Words Per Minute",
      "min": 5,
      "max": 100
    },
    {
      "name": "keyer.iambic_mode",
      "type": "enum",
      "widget": "dropdown",
      "description": "Iambic Mode",
      "values": [
        {
          "name": "ModeA",
          "description": "ModeA"
        },
        {
          "name": "ModeB",
          "description": "ModeB"
        }
      ]
    },
    {
      "name": "keyer.memory_mode",
      "type": "enum",
      "widget": "dropdown",
      "description": "Memory Mode",
      "values": [
        {
          "name": "NONE",
          "description": "NONE"
        },
        {
          "name": "DOT_ONLY",
          "description": "DOT_ONLY"
        },
        {
          "name": "DAH_ONLY",
          "description": "DAH_ONLY"
        },
        {
          "name": "DOT_AND_DAH",
          "description": "DOT_AND_DAH"
        }
      ]
    },
    {
      "name": "keyer.squeeze_mode",
      "type": "enum",
      "widget": "dropdown",
      "description": "Squeeze Timing",
      "values": [
        {
          "name": "LATCH_OFF",
          "description": "LATCH_OFF"
        },
        {
          "name": "LATCH_ON",
          "description": "LATCH_ON"
        }
      ]
    },
    {
      "name": "keyer.weight",
      "type": "u8",
      "widget": "slider",
      "description": "Dit/Dah Weight",
      "min": 33,
      "max": 67
    },
    {
      "name": "keyer.mem_window_start_pct",
      "type": "u8",
      "widget": "slider",
      "description": "Memory Window Start (%)",
      "min": 0,
      "max": 100
    },
    {
      "name": "keyer.mem_window_end_pct",
      "type": "u8",
      "widget": "slider",
      "description": "Memory Window End (%)",
      "min": 0,
      "max": 100
    },
    {
      "name": "audio.sidetone_freq_hz",
      "type": "u16",
      "widget": "slider",
      "description": "Sidetone Frequency (Hz)",
      "min": 400,
      "max": 800
    },
    {
      "name": "audio.sidetone_volume",
      "type": "u8",
      "widget": "slider",
      "description": "Sidetone Volume (%)",
      "min": 1,
      "max": 100
    },
    {
      "name": "audio.fade_duration_ms",
      "type": "u8",
      "widget": "spinbox",
      "description": "Fade Duration (ms)",
      "min": 1,
      "max": 10
    },
    {
      "name": "hardware.gpio_dit",
      "type": "u8",
      "widget": "spinbox",
      "description": "DIT Paddle GPIO",
      "min": 0,
      "max": 45
    },
    {
      "name": "hardware.gpio_dah",
      "type": "u8",
      "widget": "spinbox",
      "description": "DAH Paddle GPIO",
      "min": 0,
      "max": 45
    },
    {
      "name": "hardware.gpio_tx",
      "type": "u8",
      "widget": "spinbox",
      "description": "TX Output GPIO",
      "min": 0,
      "max": 45
    },
    {
      "name": "timing.ptt_tail_ms",
      "type": "u32",
      "widget": "slider",
      "description": "PTT Tail Timeout (ms)",
      "min": 50,
      "max": 500
    },
    {
      "name": "timing.tick_rate_hz",
      "type": "u32",
      "widget": "dropdown",
      "description": "RT Loop Tick Rate (Hz)",
      "min": 1000,
      "max": 10000
    },
    {
      "name": "system.debug_logging",
      "type": "bool",
      "widget": "toggle",
      "description": "Debug Logging"
    },
    {
      "name": "system.led_brightness",
      "type": "u8",
      "widget": "slider",
      "description": "LED Brightness (%)",
      "min": 0,
      "max": 100
    }
  ]
}
)JSON";

#endif
