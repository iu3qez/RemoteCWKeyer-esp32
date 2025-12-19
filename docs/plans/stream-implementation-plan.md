# Plan: Implement stream.rs Lock-Free SPMC Ring Buffer

## Goal

Implement proper `src/stream.rs` per TODO.md specification:
- Lock-free SPMC ring buffer with external PSRAM buffer support
- `StreamConsumer` struct with thread-local `read_idx`
- Wrap-around logic with SAFETY comments
- Comprehensive tests

## Current State Issues

The current `stream.rs` and `sample.rs` are **incompatible placeholders** that won't compile:

| Issue | Current Code | Required |
|-------|-------------|----------|
| Buffer storage | Inline array `[StreamSample; N]` | External `&'static mut [StreamSample]` for PSRAM |
| Sample field | `key: bool` | `local_key: bool` |
| Constant | `zero()` function | `EMPTY` const |
| Methods | Missing | `has_change_from()`, `with_edges_from()`, `silence()`, `silence_ticks()`, `has_local_edge()` |
| Consumer | Separate file | `StreamConsumer` struct in stream.rs |

## Implementation Plan

### Step 1: Fix `src/sample.rs` (prerequisite)

Add missing constants and methods to `StreamSample`:

```rust
// Add constant
pub const EMPTY: Self = Self::zero();

// Rename field: key → local_key
pub local_key: bool,

// Add methods for stream.rs
pub fn has_change_from(&self, other: &Self) -> bool;
pub fn with_edges_from(&self, prev: &Self) -> Self;
pub const fn silence(ticks: u32) -> Self;
pub fn is_silence(&self) -> bool;  // already exists
pub fn silence_ticks(&self) -> u32;
pub fn has_local_edge(&self) -> bool;
pub fn has_gpio_edge(&self) -> bool;
```

For `GpioState`, add public `bits` field or adjust constructors.

### Step 2: Rewrite `src/stream.rs`

Complete rewrite following TODO.md spec:

```rust
//! Lock-free SPMC ring buffer for keying events.
//! Single source of truth (ARCHITECTURE.md Rule 1.1)

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};
use crate::sample::StreamSample;

/// Lock-free SPMC ring buffer.
///
/// # Architecture (ARCHITECTURE.md)
/// - RULE 1.1.1: All keying events flow through KeyingStream
/// - RULE 3.1.1: Only atomic operations for synchronization
/// - RULE 9.1.4: PSRAM for stream buffer
pub struct KeyingStream {
    /// External buffer in PSRAM (passed at construction)
    buffer: &'static [UnsafeCell<StreamSample>],

    /// Producer write index (monotonically increasing)
    /// Only modified by single producer via fetch_add
    write_idx: AtomicUsize,

    /// Buffer capacity (power of 2 for fast modulo)
    capacity: usize,

    /// Mask for wrap-around (capacity - 1)
    mask: usize,
}

impl KeyingStream {
    /// Create stream with external PSRAM buffer.
    ///
    /// # Arguments
    /// * `buffer` - Static slice in PSRAM (300,000 samples typical)
    ///
    /// # Panics
    /// Panics if capacity is not power of 2.
    pub fn new(buffer: &'static [UnsafeCell<StreamSample>]) -> Self;

    /// Push sample to stream (producer only, RT thread).
    /// Returns false if buffer full (FAULT condition).
    ///
    /// # Timing
    /// O(1), typically < 200ns. Never blocks.
    pub fn push(&self, sample: StreamSample) -> bool;

    /// Get current write position (for consumer initialization)
    pub fn write_position(&self) -> usize;

    /// Calculate lag for consumer
    pub fn lag(&self, read_idx: usize) -> usize;

    /// Check if consumer overrun (fell too far behind)
    pub fn is_overrun(&self, read_idx: usize) -> bool;
}

/// Consumer handle for stream reading.
/// Each consumer has its own read_idx (thread-local, no sync needed).
pub struct StreamConsumer {
    stream: &'static KeyingStream,
    read_idx: usize,  // Thread-local, not atomic
}

impl StreamConsumer {
    /// Create consumer starting at current stream position.
    pub fn new(stream: &'static KeyingStream) -> Self;

    /// Read next sample (non-blocking).
    /// Returns None if caught up with producer.
    pub fn next(&mut self) -> Option<StreamSample>;

    /// Get current lag (samples behind producer).
    pub fn lag(&self) -> usize;

    /// Skip to latest position (for best-effort consumers).
    pub fn skip_to_latest(&mut self);

    /// Resync after overrun.
    pub fn resync(&mut self);
}
```

### Step 3: Update `src/consumer.rs`

Modify `HardRtConsumer` and `BestEffortConsumer` to use the new `StreamConsumer` or adapt to the new API. Options:

**Option A**: Keep consumer.rs, update to use new stream API
**Option B**: Integrate consumer logic into `StreamConsumer` with a `ConsumerMode` enum

Recommend **Option A** for cleaner separation of concerns.

### Step 4: Fix downstream files

Update files using old field names:
- `src/iambic.rs`: `local_key` → already correct
- `src/main.rs`: `local_key` → already correct
- `src/consumer.rs`: Update for new stream API

### Step 5: Comprehensive Tests

Add tests in `stream.rs`:

```rust
#[cfg(test)]
mod tests {
    // 1. Basic write/read
    #[test]
    fn test_single_write_read() { ... }

    // 2. Wrap-around behavior
    #[test]
    fn test_buffer_wrap_around() { ... }

    // 3. Lag calculation
    #[test]
    fn test_lag_calculation() { ... }

    // 4. Multiple consumers
    #[test]
    fn test_multiple_consumers_independent() { ... }

    // 5. Overrun detection
    #[test]
    fn test_overrun_detection() { ... }

    // 6. Consumer skip_to_latest
    #[test]
    fn test_skip_to_latest() { ... }

    // 7. Silence compression (if keeping RLE)
    #[test]
    fn test_silence_compression() { ... }
}
```

## Files to Modify

| File | Action |
|------|--------|
| `src/sample.rs` | Add `EMPTY`, rename `key`→`local_key`, add methods |
| `src/stream.rs` | Complete rewrite with PSRAM buffer + StreamConsumer |
| `src/consumer.rs` | Update to use new stream API |
| `src/iambic.rs` | Verify field names (should work) |
| `src/main.rs` | Verify field names (should work) |
| `src/lib.rs` | Update exports if needed |

## Memory Ordering Rules (ARCHITECTURE.md §3.2)

```rust
// Producer: AcqRel for read-modify-write
let idx = write_idx.fetch_add(1, Ordering::AcqRel);

// Consumer: Acquire for read
let head = write_idx.load(Ordering::Acquire);
```

## SAFETY Comment Template

Every `unsafe` block must have:

```rust
// SAFETY: <why unsafe is needed>
// Invariants:
// - <invariant 1>
// - <invariant 2>
unsafe {
    ...
}
```

## Design Decisions (Confirmed)

1. **Buffer API**: External buffer ONLY (per TODO.md spec)
   - `KeyingStream::new(buffer: &'static [UnsafeCell<StreamSample>])`
   - No const generic version
   - Buffer allocated in PSRAM externally, passed to stream

2. **Silence compression**: RLE in stream.rs `push()`
   - Stream compresses silence at write time
   - Saves memory (5x reduction typical)
   - Consumers see compressed silence markers

## Estimated Changes

- `sample.rs`: ~50 lines added
- `stream.rs`: ~250-300 lines (rewrite)
- `consumer.rs`: ~30 lines modified
- Tests: ~150 lines
- **Total**: ~450-500 lines
