# ISR Paddle Input - Pre-Mortem Mitigations

**Date**: 2026-01-12
**Branch**: `feature/isr-paddle-input`
**Status**: PROPOSED

---

## Executive Summary

Pre-mortem analysis of the ISR-based paddle input implementation identified 3 tigers (risks) and 1 elephant. This document proposes mitigations to address the HIGH severity risks before merging.

---

## Identified Risks

### [TIGER HIGH] No Recovery if Interrupt Stays Disabled

**Location**: `hal_gpio.c:63-73`

**Problem**: The ISR handler disables the GPIO interrupt and relies on an esp_timer to re-enable it after the blanking period. If:
- `esp_timer_start_once()` fails silently
- The timer task is starved and never executes
- Any other failure prevents the callback from running

...the interrupt remains disabled **permanently**. The only recovery is reboot.

**Current State**: No mitigation present. No watchdog. No periodic check.

### [TIGER MEDIUM] Blanking Timer Runs in Task Context

**Location**: `hal_gpio.c:114` (`ESP_TIMER_TASK` dispatch method)

**Problem**: The blanking timer callback runs in the `esp_timer` FreeRTOS task, not in ISR context. Under heavy CPU load on Core 0, this callback could be delayed beyond the nominal blanking period.

**Impact**: Variable blanking duration (nominal + task scheduling jitter). For 3000µs blanking, this could add 10-100µs in worst case.

**Assessment**: Low practical impact - blanking is already long. But contributes to unpredictability.

### [TIGER MEDIUM] Polling Fallback Not Truly Independent

**Location**: `rt_task.c` ISR integration

**Problem**: If ISR gets stuck-disabled, the system falls back to polling-only. While this works, it loses the latency improvement that ISR was meant to provide. User may not notice degradation.

**Assessment**: Functional but defeats purpose of ISR. Should alert or auto-recover.

### [TIGER NEW] Blanking Duration vs QRQ Speed Conflict

**Location**: `parameters.yaml:isr_blanking_us` (default: 2000µs)

**Problem**: Blanking period creates a window where keypresses are ignored.
- If blanking is too long → misses rapid QRQ keypresses
- If blanking is too short → bounce causes ISR storms

**QRQ Reality** (non-standard spacing):
```
Standard CW (PARIS):
  dit : dah : space = 1 : 3 : 1

QRQ operators often use:
  dit : dah : space = 1 : 4+ : 0.5  (compressed inter-element)

Result: dit=5ms but inter-element space might be only 2-3ms!
```

**Constraint**: For aggressive QRQ:
- Dit duration: ~5ms
- Inter-element space: **2-3ms** (not 5ms!)
- Blanking = 2ms = 66-100% of window!

**If inter-element space = 2ms and blanking = 2ms**:
- Window fully consumed by blanking
- **100% probability of missing rapid keypresses**

**This is likely why previous ISR implementation "didn't work"!**

**Mitigation Options**:

1. **Ultra-short blanking for QRQ** (1000µs)
   - Pro: Allows 1ms for keypress detection
   - Con: May not cover all bounce (risky for poor paddles)

2. **Hybrid approach**: ISR for first edge only, polling for timing
   - ISR catches the press instantly
   - Polling handles the sustained/release detection
   - Blanking only needs to cover the initial bounce spike (~500µs)

3. **Hardware debounce + short software blanking**
   - Add RC filter on GPIO input (10k + 100nF = 1ms τ)
   - Software blanking: 500µs (only for residual bounce)
   - Total effective debounce: ~1.5ms

4. **Schmitt trigger input with hysteresis** ⭐ APPROFONDIMENTO

   **Cos'è uno Schmitt Trigger:**
   ```
   Input noisy (bounce):          Schmitt Output (clean):
        ___                            ___________
   ____/   \__/\__/\_____        _____/           \_____
        ↑    ↑  ↑  ↑                   ↑           ↑
       bounce noise                single clean edge

   Funzionamento: due soglie diverse (isteresi)
   ┌─────────────────────────────────────┐
   │  VCC ─────────────────────────────  │
   │                                     │
   │  VIH ──────────────── 2.0V ← rising │
   │       ↑ hysteresis                  │
   │  VIL ──────────────── 0.8V ← falling│
   │                                     │
   │  GND ─────────────────────────────  │
   └─────────────────────────────────────┘

   Il bounce (tipicamente 0.2-0.5V ampiezza) non attraversa
   mai entrambe le soglie → output stabile!
   ```

   **Opzioni implementative:**

   **A. ESP32-S3 Built-in (limitato)**
   - GPIO ha Schmitt trigger interno ma NON configurabile via API
   - Isteresi ~0.25V (troppo bassa per bounce meccanico)
   - Non sufficiente per paddle di bassa qualità

   **B. GPIO Glitch Filter (ESP-IDF 5.x)** ❌ NON ADATTO

   Da documentazione Espressif:
   > "hardware filters to remove unwanted glitch pulses...
   > filter out pulses shorter than two sample clock cycles.
   > The duration of the filter is not configurable."

   ```c
   #include "driver/gpio_filter.h"

   gpio_glitch_filter_handle_t filter;
   gpio_pin_glitch_filter_config_t config = {
       .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
       .gpio_num = GPIO_DIT_PIN,
   };
   gpio_new_pin_glitch_filter(&config, &filter);
   gpio_glitch_filter_enable(filter);
   ```

   **Calcolo:**
   - IO_MUX clock: 80MHz (APB)
   - 2 cicli = 2 × 12.5ns = **25ns**
   - Bounce meccanico: **100µs - 10ms**

   ```
   25ns  vs  100,000ns (100µs minimo bounce)
         ↓
   Rapporto: 1:4000 → COMPLETAMENTE INUTILE per debounce!
   ```

   **Uso corretto**: Filtrare glitch da EMI/rumore elettrico, non bounce meccanico.

   **C. Hardware esterno: 74HC14 (RACCOMANDATO per QRQ)**
   ```
                    ┌─────────┐
   Paddle ──┬── R1 ─┤1   14├─ VCC (3.3V)
            │ 10kΩ  │ 74HC14 │
           ─┴─      │ (hex   │
           GND      │ Schmitt├── GPIO ESP32
                    │inverter│
                    └────────┘

   Costo: ~€0.30 (6 inverter, ne servono 2)

   Specifiche 74HC14 @ 3.3V:
   - VIH (positive threshold): 1.9V
   - VIL (negative threshold): 1.2V
   - Hysteresis: 0.7V ✓ (copre bounce 0.2-0.5V)
   - Propagation delay: ~10ns (trascurabile)
   ```

   **D. RC + Schmitt (ultra-robusto per paddle economici)**
   ```
                    100nF
   Paddle ──┬── R ───┬────┬── 74HC14 ── GPIO
            │  10kΩ  │    │
           ─┴─      ─┴─  ─┴─
           GND      GND  VCC

   Filtro RC + Schmitt trigger:
   - RC τ = 10kΩ × 100nF = 1ms
   - Filtra bounce < 1ms prima del Schmitt
   - Software blanking: può essere 200-500µs!
   - Latenza aggiunta: ~2.2τ = 2.2ms (rise time 10%-90%)
   ```

   **E. Optoisolatore (PC817 o simile)** ⭐ SOLUZIONE SCELTA

   ```
            330Ω         PC817
   Paddle ──/\/\/──┬────►|────┐
                   │    LED   │ Fototrans.
                  ─┴─        ─┴─ 10kΩ
                  GND         │  pull-up
                              └── GPIO ESP32
   ```

   **Vantaggi (due piccioni, una fava):**
   - ✅ Isolamento galvanico (protezione EMI/ESD)
   - ✅ Soglia LED ~1.1V >> bounce 0.2-0.5V
   - ✅ Rise time ~10µs = filtro passa-basso naturale
   - ✅ Già necessario per isolamento → nessun componente extra

   **Specifiche PC817 @ 5mA If:**
   - Vf (forward): 1.0-1.2V (soglia implicita)
   - Rise time: 3-18µs (filtra glitch rapidi)
   - CTR: 50-600% (margine ampio)
   - Isolamento: 5kV (protezione robusta)

   **Calcolo debounce:**
   ```
   Bounce spike tipico: 0.3V, durata 100µs
   Soglia LED: 1.1V
   0.3V < 1.1V → spike ignorato ✓

   Rise time 10µs → filtra glitch < 10µs
   Bounce > 10µs ma < soglia → comunque filtrato ✓
   ```

   **Confronto soluzioni:**

   | Soluzione | Blanking SW | Latenza HW | Costo | QRQ OK? | Isolamento |
   |-----------|-------------|------------|-------|---------|------------|
   | Solo SW (attuale) | 2000µs | 0 | €0 | ⚠️ | ❌ |
   | GPIO Glitch Filter | 2000µs | ~ns | €0 | ⚠️ | ❌ |
   | 74HC14 | 500µs | ~10ns | €0.30 | ✅ | ❌ |
   | **Optoisolatore** | **500µs** | **~10µs** | **€0.20** | **✅** | **✅** |
   | RC + 74HC14 | 200µs | ~2ms | €0.50 | ❌ | ❌ |

   **Raccomandazione**: Optoisolatore (PC817)
   - Isolamento galvanico + debounce in un componente
   - Latenza hardware: ~10µs (accettabile)
   - Permette blanking SW ridotto a 500µs
   - Per QRQ: inter-element 2ms - 500µs = **1.5ms finestra** ✓
   - Nota: logica da verificare (dipende da circuito pull-up/down)

**Recommended Solution**: Option 2 (Hybrid) + Adaptive Blanking

---

### 5. **MITIGAZIONE SOFTWARE IMMEDIATA** (in attesa di hardware robusto)

**Obiettivo**: Supportare QRQ senza modifiche hardware, accettando compromessi.

#### 5.1 Adaptive Blanking

```c
// hal_gpio.c - Blanking adattivo basato su WPM

static uint32_t s_effective_blanking_us = 1500;  // Default conservativo

/**
 * @brief Ricalcola blanking ottimale per WPM corrente
 * @param wpm Velocità in words per minute
 *
 * Formula: blanking = min(base, 40% del worst-case inter-element)
 * Worst-case inter-element per QRQ = dit_duration / 2
 */
void hal_gpio_update_blanking_for_wpm(uint32_t wpm) {
    const uint32_t BASE_BLANKING_US = 1500;
    const uint32_t MIN_BLANKING_US = 500;   // Sotto questo: rischio bounce

    // Dit duration: 1,200,000 / WPM (in µs)
    uint32_t dit_us = 1200000 / wpm;

    // QRQ worst case: inter-element = 50% dit
    uint32_t inter_element_us = dit_us / 2;

    // Blanking max = 40% inter-element (60% finestra per detection)
    uint32_t max_blanking = (inter_element_us * 40) / 100;

    // Clamp
    s_effective_blanking_us = BASE_BLANKING_US;
    if (s_effective_blanking_us > max_blanking) {
        s_effective_blanking_us = max_blanking;
    }
    if (s_effective_blanking_us < MIN_BLANKING_US) {
        s_effective_blanking_us = MIN_BLANKING_US;
    }

    ESP_LOGI(TAG, "Adaptive blanking: WPM=%lu → blanking=%luµs",
             (unsigned long)wpm, (unsigned long)s_effective_blanking_us);
}

// ISR usa blanking adattivo
static void IRAM_ATTR dit_isr_handler(void *arg) {
    atomic_store_explicit(&s_dit_pending, true, memory_order_release);
    gpio_intr_disable((gpio_num_t)s_config.dit_pin);
    esp_timer_start_once(s_dit_blanking_timer, s_effective_blanking_us);  // ← Adattivo!
}
```

#### 5.2 Tabella Comportamento

| WPM | Dit | Inter-elem (QRQ) | Blanking Calcolato | Finestra |
|-----|-----|------------------|-------------------|----------|
| 40  | 30ms | 15ms | 1500µs (base) | 13.5ms ✅ |
| 60  | 20ms | 10ms | 1500µs (base) | 8.5ms ✅ |
| 80  | 15ms | 7.5ms | 1500µs (base) | 6ms ✅ |
| 120 | 10ms | 5ms | 1500µs (base) | 3.5ms ✅ |
| 160 | 7.5ms | 3.75ms | 1500µs (max) | 2.25ms ✅ |
| 200 | 6ms | 3ms | **1200µs** (auto) | 1.8ms ⚠️ |
| 240 | 5ms | 2.5ms | **1000µs** (auto) | 1.5ms ⚠️ |
| 300 | 4ms | 2ms | **800µs** (auto) | 1.2ms ⚠️ |

**Legenda**:
- ✅ Margine confortevole
- ⚠️ Al limite - funziona con paddle di buona qualità

#### 5.3 Fallback Polling Rafforzato

```c
// rt_task.c - Polling come safety net

static int64_t s_last_dit_poll_us = 0;
static bool s_dit_poll_state = false;

// Nel loop RT:
gpio_state_t gpio = hal_gpio_read_paddles();

// ISR detection (priorità)
if (hal_gpio_consume_dit_press()) {
    gpio = gpio_with_dit(gpio, true);
}

// Polling fallback con debounce software minimale
bool dit_now = gpio_get_dit(gpio);
if (dit_now && !s_dit_poll_state) {
    // Rising edge via polling
    int64_t elapsed = now_us - s_last_dit_poll_us;
    if (elapsed > 500) {  // 500µs debounce minimo
        // Valida press via polling (ISR potrebbe aver perso)
        gpio = gpio_with_dit(gpio, true);
    }
    s_last_dit_poll_us = now_us;
}
s_dit_poll_state = dit_now;
```

#### 5.4 Warning per Utente

```c
// Quando WPM > 150 e blanking adattivo attivo
if (wpm > 150 && s_effective_blanking_us < 1500) {
    ESP_LOGW(TAG, "QRQ mode: blanking ridotto a %luµs. "
             "Per prestazioni ottimali, considera hardware debounce (74HC14).",
             (unsigned long)s_effective_blanking_us);
}
```

#### 5.5 Parametri Configurabili

In `parameters.yaml`:
```yaml
hardware:
  isr_blanking_us:
    type: u32
    default: 1500          # Ridotto da 2000
    range: [500, 5000]
    nvs_key: "isr_blank"

  isr_adaptive_blanking:
    type: bool
    default: true          # NUOVO: abilita auto-riduzione
    nvs_key: "isr_adapt"

  isr_min_blanking_us:
    type: u32
    default: 500           # NUOVO: floor per adaptive
    range: [200, 1000]
    nvs_key: "isr_minbl"
```

---

**Hybrid ISR + Polling Pattern** (riferimento):
```c
// ISR: Only set "press detected" flag, no timing responsibility
static void IRAM_ATTR dit_isr_handler(void *arg) {
    atomic_store(&s_dit_press_detected, true);  // Instant detection
    gpio_intr_disable(s_config.dit_pin);
    // Blanking adattivo invece di fisso
    esp_timer_start_once(s_dit_blanking_timer, s_effective_blanking_us);
}

// RT task: Use polling for release/sustained, ISR flag for press
gpio_state_t gpio = hal_gpio_read_paddles();  // Polling
if (hal_gpio_consume_dit_press()) {
    // ISR detected press - trust it even if polling shows release
    // (contact might still be bouncing)
    gpio = gpio_with_dit(gpio, true);
}
```

**Status**: NEEDS_MITIGATION - recommend reducing default blanking to 1000µs and testing

### [ELEPHANT RESOLVED] Recursive ISRs from Contact Bouncing

**Root Cause Identified**: The previous ISR implementation suffered from **recursive/rapid ISR invocations** caused by mechanical contact bounce.

**What Happened**:
```
Contact pressed
   ↓
First falling edge → ISR fires
   ↓
Contact bounces (mechanical)
   ↓
Multiple falling edges in ~1-5ms
   ↓
Each edge triggers ISR → ISR storm!
   ↓
Stack overflow / watchdog timeout / system hang
```

**Typical bounce characteristics**:
- Duration: 1-10ms (varies by switch quality)
- Pattern: Multiple rapid open/close cycles
- Frequency: Can exceed 100 transitions in first millisecond

**How Current Implementation Addresses This**:

The new design uses **immediate interrupt disable + blanking timer**:

```c
static void IRAM_ATTR dit_isr_handler(void *arg) {
    atomic_store_explicit(&s_dit_pending, true, ...);  // Flag for RT task
    gpio_intr_disable(s_config.dit_pin);               // ← CRITICAL: Stop more ISRs
    esp_timer_start_once(s_dit_blanking_timer, 2000);  // Re-enable after 2ms
}
```

**Timeline with fix**:
```
t=0µs:    Falling edge → ISR fires
t=1-5µs:  ISR sets flag, disables interrupt, starts timer
t=6µs:    ISR returns
t=100µs:  Bounce edge (ignored - interrupt disabled!)
t=500µs:  More bounce (ignored)
t=2000µs: Blanking timer fires → re-enable interrupt
          (Contact has settled by now)
```

**Status**: ✅ MITIGATED by design

**Remaining Concerns**:

1. **Blanking vs QRQ Speed Trade-off**:

   | WPM | Dit Duration | Inter-element | Max Blanking |
   |-----|--------------|---------------|--------------|
   | 40  | 30ms         | 30ms          | 15ms (50%)   |
   | 60  | 20ms         | 20ms          | 10ms (50%)   |
   | 80  | 15ms         | 15ms          | 7ms (50%)    |
   | QRQ (dit=5ms) | 5ms | 5ms        | **2ms (40%)** |

   **Constraint**: Blanking MUST be < 50% of inter-element space to avoid missing rapid presses.

   For QRQ operators with dit=5ms: **blanking ≤ 2000µs** (current default is 2000µs ✓)

2. **Blanking too long**: Misses rapid keypresses → operator feels "sluggish"
3. **Blanking too short**: Bounce triggers multiple ISRs → ISR storm

**Recommendation**:
- Default: 2000µs (current) - safe for QRQ
- Maximum: 3000µs - only for poor-quality paddles, warns user of speed limit
- Adaptive: Consider adjusting based on WPM setting (blanking = min(2000, dit_duration * 0.3))

**Formula for adaptive blanking**:
```c
uint32_t dit_us = iambic_dit_duration_us(&config);  // from WPM
uint32_t max_blanking = dit_us * 30 / 100;          // 30% of dit
uint32_t blanking = MIN(s_config.isr_blanking_us, max_blanking);
```

---

## Proposed Mitigations

### Mitigation 1: ISR Watchdog (addresses TIGER HIGH + MEDIUM)

Add a watchdog mechanism in the RT task that detects and recovers from stuck interrupts.

#### Design

```c
// hal_gpio.c additions

// Track when each interrupt was disabled
static volatile int64_t s_dit_disabled_at_us = 0;
static volatile int64_t s_dah_disabled_at_us = 0;

// Update ISR handlers to record disable timestamp
static void IRAM_ATTR dit_isr_handler(void *arg) {
    (void)arg;
    atomic_store_explicit(&s_dit_pending, true, memory_order_release);
    atomic_fetch_add_explicit(&s_dit_isr_count, 1, memory_order_relaxed);

    // NEW: Record when we disabled the interrupt
    s_dit_disabled_at_us = esp_timer_get_time();

    gpio_intr_disable((gpio_num_t)s_config.dit_pin);
    esp_timer_start_once(s_dit_blanking_timer, (uint64_t)s_config.isr_blanking_us);
}

// Update blanking timer callbacks to clear timestamp
static void dit_blanking_expired(void *arg) {
    (void)arg;
    s_dit_disabled_at_us = 0;  // NEW: Mark as not disabled
    gpio_intr_enable((gpio_num_t)s_config.dit_pin);
}

// NEW: Watchdog function - call from RT task every tick
void hal_gpio_isr_watchdog(int64_t now_us) {
    if (!s_isr_enabled) return;

    // Max time interrupt can be disabled: blanking + 1ms safety margin
    const int64_t max_disabled_us = (int64_t)s_config.isr_blanking_us + 1000;

    // Check DIT
    int64_t dit_disabled = s_dit_disabled_at_us;
    if (dit_disabled > 0 && (now_us - dit_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dit_pin);
        s_dit_disabled_at_us = 0;
        atomic_fetch_add_explicit(&s_watchdog_recovery_count, 1, memory_order_relaxed);
        // RT_WARN would go here but we're in RT path - use counter instead
    }

    // Check DAH (same logic)
    int64_t dah_disabled = s_dah_disabled_at_us;
    if (dah_disabled > 0 && (now_us - dah_disabled) > max_disabled_us) {
        gpio_intr_enable((gpio_num_t)s_config.dah_pin);
        s_dah_disabled_at_us = 0;
        atomic_fetch_add_explicit(&s_watchdog_recovery_count, 1, memory_order_relaxed);
    }
}
```

#### RT Task Integration

```c
// rt_task.c - in main loop

for (;;) {
    int64_t now_us = esp_timer_get_time();

    // NEW: ISR health check (adds ~20 instructions, negligible)
    hal_gpio_isr_watchdog(now_us);

    // ... rest of RT loop unchanged ...
}
```

#### API Addition

```c
// hal_gpio.h

/**
 * @brief Check ISR health and recover from stuck interrupts
 * @param now_us Current timestamp from esp_timer_get_time()
 * @note RT-safe, call from RT task every tick
 */
void hal_gpio_isr_watchdog(int64_t now_us);

/**
 * @brief Get watchdog recovery count
 * @return Number of times watchdog had to recover a stuck interrupt
 */
uint32_t hal_gpio_get_watchdog_recoveries(void);
```

#### Performance Impact

- **Cost per tick**: ~20 CPU instructions (2 comparisons, 2 atomic loads)
- **At 1kHz tick rate**: 20,000 instructions/second = negligible
- **Latency budget**: Well within 100µs ceiling

---

### Mitigation 2: Consider ESP_TIMER_ISR (Optional Enhancement)

For tighter blanking timing, switch to ISR-context timer dispatch:

```c
esp_timer_create_args_t dit_timer_args = {
    .callback = dit_blanking_expired,
    .dispatch_method = ESP_TIMER_ISR,  // Instead of ESP_TIMER_TASK
    .name = "dit_blank",
};
```

**Requirements**:
- Callback MUST be `IRAM_ATTR`
- Callback MUST be very short (enable GPIO only)
- No logging, no heap, no blocking

**Assessment**: Current implementation already satisfies these. Change is low-risk.

**Recommendation**: Implement after validating Mitigation 1 works.

---

### Mitigation 3: Diagnostic Visibility

Add console commands to inspect ISR health:

```
> isr status
ISR enabled: yes
Blanking: 3000us
DIT triggers: 1234
DAH triggers: 567
Watchdog recoveries: 0  <-- Non-zero indicates problems
```

Already partially implemented via `hal_gpio_isr_get_stats()`. Extend with watchdog counter.

---

## Implementation Priority

| Mitigation | Priority | Effort | Risk Addressed |
|------------|----------|--------|----------------|
| ISR Watchdog | **P0** | 1-2 hours | HIGH tiger |
| ESP_TIMER_ISR | P2 | 30 min | MEDIUM tiger |
| Diagnostic visibility | P1 | 30 min | Observability |

---

## Testing Plan

1. **Normal operation**: Verify ISR latency improvement (oscilloscope)
2. **Watchdog trigger**: Artificially block esp_timer task, verify recovery
3. **Stress test**: 40+ WPM keying for extended period
4. **GPIO conflict**: Test on ESP32-S3 with JTAG pins
5. **Regression**: Compare timing precision vs polling-only baseline

---

## Pre-Merge Checklist

- [ ] Implement ISR Watchdog (Mitigation 1)
- [ ] Add watchdog counter to `isr status` command
- [ ] Test watchdog recovery path
- [ ] Document past failure mode (ELEPHANT)
- [ ] Oscilloscope verification of latency improvement
- [ ] 30-minute stress test at 40 WPM

---

## Appendix: Risk Analysis Summary

```yaml
premortem:
  date: 2026-01-12
  mode: quick
  branch: feature/isr-paddle-input

  tigers:
    - risk: "No recovery if interrupt stays disabled"
      severity: HIGH
      status: MITIGATION_PROPOSED (watchdog)

    - risk: "Blanking duration vs QRQ speed conflict"
      severity: HIGH
      status: NEEDS_MITIGATION
      detail: "2ms blanking with 2-3ms QRQ inter-element = missed keypresses"
      recommendation: "Reduce to 500-1000µs, use hybrid ISR+polling"

    - risk: "Blanking timer in task context"
      severity: MEDIUM
      status: ACCEPTED_WITH_WATCHDOG

    - risk: "Polling fallback not independent"
      severity: MEDIUM
      status: COVERED_BY_WATCHDOG

  elephants:
    - risk: "Recursive ISRs from contact bouncing"
      severity: HIGH
      status: RESOLVED
      detail: "Previous implementation lacked interrupt disable in ISR"
      current_mitigation: "gpio_intr_disable() called immediately in ISR"

  paper_tigers:
    - risk: "GPIO4 JTAG conflict"
      status: MITIGATED (force_gpio_reset)

    - risk: "Memory ordering"
      status: CORRECT (release/acquire)

  key_insight: |
    QRQ operators use non-standard timing (dit:dah:space ≠ 1:3:1).
    Inter-element space can be as short as 2ms.
    Blanking period MUST be < inter-element space.
    Current 2ms blanking is AT THE LIMIT for QRQ.
```
