# RemoteCWKeyerV3 - Code Style Guidelines

## File Header Comments

**MANDATORY**: Every source file (`.rs`) MUST start with a header comment block explaining:

1. **What** the module does (purpose)
2. **Why** it exists (architectural role)
3. **Key constraints** (RT-safe? Lock-free? No allocation?)

### Format:

```rust
//! Module: <module_name>
//!
//! Purpose: <1-2 sentence description>
//!
//! Architecture:
//! - <Key architectural constraint 1>
//! - <Key architectural constraint 2>
//! - <Relationship to ARCHITECTURE.md principles>
//!
//! Safety: <RT-safe/Best-effort/Has unsafe blocks/etc>
```

### Example:

```rust
//! Module: stream
//!
//! Purpose: Lock-free SPMC ring buffer for keying events. Single source of
//! truth for all GPIO, local key, and remote key state.
//!
//! Architecture:
//! - Zero mutexes, atomic operations only (ARCHITECTURE.md Rule 3.1)
//! - Single producer (RT thread), multiple consumers (RT + best-effort)
//! - Silence compression via run-length encoding
//!
//! Safety: Contains unsafe blocks for lock-free ring buffer indexing.
//! All unsafe blocks have SAFETY comments explaining invariants.
```

---

## Code Documentation

### Principle: **Self-Documenting Code with Strategic Comments**

1. **Functions**: Doc comment (`///`) explaining:
   - What it does
   - Key parameters
   - Return value semantics
   - Panics/errors (if any)
   - RT-safety if relevant

2. **Structs/Enums**: Doc comment explaining:
   - Purpose
   - Invariants
   - Usage context

3. **Complex logic**: Inline comments (`//`) explaining **why**, not **what**
   - Good: `// Wait for idle to avoid corrupting timing mid-symbol`
   - Bad: `// Check if paddles are idle`

4. **Unsafe blocks**: `// SAFETY:` comment MANDATORY
   - Explain why unsafe is needed
   - Document invariants that make it safe

---

## Rust Idioms

### Naming:
- `snake_case` for functions, variables, modules
- `CamelCase` for types, traits
- `SCREAMING_SNAKE_CASE` for constants, statics
- Suffix `_us`, `_ms`, `_hz` for units (e.g., `dit_duration_us`)

### Atomics:
- Always specify `Ordering` explicitly (never default)
- Comment why chosen ordering is correct
- RT path: prefer `Relaxed` when safe, `Acquire`/`Release` for synchronization

### Error handling:
- RT path: avoid `Result`, prefer FAULT on error
- Best-effort: `Result` or `Option` acceptable

---

## Comments Density

**Target**: 1 comment every 5-10 lines of non-trivial code.

**Trivial code** (no comment needed):
```rust
let x = y + 1;
self.counter += 1;
```

**Non-trivial code** (comment required):
```rust
// Spin-wait to next tick (busy-wait acceptable on dedicated RT core)
while esp_timer_get_time() - start_us < TICK_PERIOD_US {
    core::hint::spin_loop();
}
```

---

## File Organization

Within a `.rs` file:

1. File header comment (mandatory)
2. Imports (`use` statements)
3. Constants/statics
4. Type definitions (structs, enums)
5. Implementations
6. Private helper functions
7. Tests (`#[cfg(test)]`)

---

---

## Configuration Parameters

### MANDATORY: All parameters in `parameters.yaml`

**RULE**: Every configuration parameter MUST be defined in `parameters.yaml`.

**Forbidden:**
```rust
// ❌ WRONG - hardcoded config value
const WPM: u16 = 20;
const SIDETONE_FREQ: u16 = 700;
```

**Required:**
```rust
// ✅ CORRECT - read from generated config
let wpm = CONFIG.wpm.load(Ordering::Relaxed);
let freq = CONFIG.sidetone_freq_hz.load(Ordering::Relaxed);
```

**Why:**
- Single source of truth for all configuration
- Automatic GUI generation
- NVS persistence
- Multi-language support
- Type safety and validation

**Process to add new parameter:**
1. Add entry to `parameters.yaml` with all metadata
2. Run code generation: `python3 scripts/gen_config.py`
3. Use generated struct in code: `CONFIG.your_param`

**Never:**
- Hardcode magic numbers for configurable values
- Add parameters directly in Rust code
- Bypass the schema

---

## Compliance

Code reviews must verify:
- [ ] File header present and accurate
- [ ] All `unsafe` blocks have `SAFETY` comments
- [ ] Complex logic has explanatory comments
- [ ] Public API has doc comments (`///`)
- [ ] RT-safety explicitly documented where relevant
- [ ] All configuration parameters defined in `parameters.yaml`
- [ ] No hardcoded config values in source code

**Non-compliant code shall not be merged.**
