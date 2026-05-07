# Waterfall Draw vs TX Timing — Design Notes

## Problem statement

FT4 TX fails to decode on hardware. Root cause: the main loop polls `tx_tick()`
at intervals that aren't divisible by FT4's 48 ms symbol period, creating
inter-symbol interference (ISI) that prevents LDPC from converging.

### Timing chain (pre-fix)

```
Main loop iteration:
  tx_tick()               ~0 ms  (non-blocking, returns if not yet time)
  ui_draw_waterfall()    ~43 ms  (4,320 drawPixel() calls × ~10 µs each)
  vTaskDelay(2)            2 ms
  ─────────────────────────────
  Effective poll period: ~45 ms

FT4 symbol period: 48 ms
48 % 45 ≠ 0  →  first tone fires at 90 ms instead of 48 ms  →  ISI  →  FAIL
```

FT8 (160 ms symbols) is unaffected because its much longer period tolerates
larger per-poll jitter.

### Validated by tests

- **test_l3** (`l3_poll_timing`): confirms which poll intervals cause ISI vs not
- **test_l4** (`l4_timer_isolation`): confirms COUPLED (main-loop) vs ISOLATED
  (hardware-timer) architectural model; validates that 45 ms effective poll fails
  and 2 ms timer period succeeds regardless of UI blocking

---

## Current memory budget (ESP32-S3FN8)

Chip: **512 KB internal SRAM, no PSRAM**. Full stop — FN8 has no PSRAM.

### Static picture (from `idf.py size`)

| Memory region | Total    | Used (static) | Used % | Remaining |
|---------------|----------|---------------|--------|-----------|
| DIRAM         | 341,760 B | 265,291 B    | 77.6%  | **76,469 B** |
| IRAM          |  16,384 B |  16,384 B    | **100%** | **0 B** |
| RTC FAST      |   8,192 B |     132 B    |  1.6%  | 8,060 B |
| RTC SLOW      |   8,192 B |      32 B    |  0.4%  | 8,160 B |

### Largest static consumers of DIRAM

| Component        | .bss     | Notes |
|------------------|----------|-------|
| libft8_lib.a     | 95,923 B | Decoder working memory (static allocation) |
| libmain.a        | 21,247 B | App globals |
| libfreertos.a    | 20,604 B | Task stacks, queues |
| libui.a          | 11,242 B | Display state |
| libesp_hw_support| 12,328 B | Hardware drivers |

### What the 76 KB remaining DIRAM actually is

The "remaining" figure is **the starting heap size** before any runtime
allocation. It must cover:
- FreeRTOS task stacks not already counted (each new task costs 2–8 KB)
- Audio DMA buffers (allocated at init)
- BLE/NimBLE connection state (~30 KB active)
- Any `malloc()` calls during operation

**A one-time probe before committing to any option:** add this log line at the
end of `app_main()` after all init:
```cpp
ESP_LOGI(TAG, "heap free=%u, largest_dma_block=%u",
         (unsigned)esp_get_free_heap_size(),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
```
The `largest_dma_block` number is the ceiling for any single `malloc` including
an off-screen frame buffer.

### Critical hard constraint: IRAM is 100% full

IRAM = 0 bytes remaining. This has two direct consequences for the design:
1. **No new `IRAM_ATTR` functions** — any new code that needs IRAM will fail
   to link.
2. **`ESP_TIMER_ISR` dispatch is blocked** — ISR-mode timer callbacks require
   the handler to live in IRAM. Must use `ESP_TIMER_TASK` dispatch instead
   (runs in a dedicated FreeRTOS task, which is fine for our use case since
   `tx_tick()` calls UART and ESP_LOG which aren't ISR-safe anyway).

---

## The four design levers

Every solution reduces to pulling one or more of these:

| Lever | Mechanism | Complexity | UI quality during TX |
|-------|-----------|------------|----------------------|
| **A. Don't do the work** | Guard with `if (!g_tx_active)` | Trivial | Frozen waterfall |
| **B. Slice the work** | Draw N rows/pixels per loop iteration | Medium | Slow refresh |
| **C. Speed up the work** | Bulk blit (`pushImage` / DMA) instead of per-pixel | Medium | Full refresh |
| **D. Isolate the work** | `tx_tick()` in `esp_timer` periodic callback | High | Full refresh |

---

## Option A — Suppress waterfall during TX (implemented)

**Status:** done — three call sites guarded with `if (!g_tx_active)`.

The effective main-loop period during TX drops from ~45 ms to ~2 ms:
```
tx_tick()      ~0 ms
(waterfall skipped)
vTaskDelay(2)   2 ms
─────────────────
Effective: 2 ms   →   48 % 2 = 0   →   zero jitter   →   PASS
```

**Trade-off:** waterfall display freezes for the duration of TX (≤7.5 s FT4,
≤15 s FT8). Acceptable for beacon/contest operation; slightly annoying for
interactive QSO use. During TX the device isn't receiving anyway.

**Robustness:** brittle. Any new blocking UI work added to the main loop during
TX will reintroduce jitter. The `if (!g_tx_active)` pattern has to be applied
to every future addition.

---

## Option B — Chunked drawing

Draw a fixed number of rows (or pixels) per loop iteration, carrying partial-draw
state between calls.

### Math

- Waterfall: 18 rows × 240 pixels = 4,320 pixels at ~10 µs each = ~43 ms total
- Budget per loop: to keep `max_late ≤ 5 ms` (safe decoder margin with hardware
  SNR), each chunk must complete in `< 3 ms` → `< 300 pixels → ~1.25 rows`
- Practical: 1 row per iteration (240 px × 10 µs = 2.4 ms per chunk)
- Effective poll: 2 + 2.4 = 4.4 ms; `48 % 4.4 ≈ 4` ms max_late — marginal
- Better: 120 px per chunk (½ row × 10 µs = 1.2 ms); effective = 3.2 ms;
  48 % 3.2 ≈ 3.2 ms max_late — acceptable at clean signal, tight at hardware SNR

### Trade-off

- **Pro:** no extra RAM, keeps UI alive
- **Con:** still serial, still vulnerable to other new blocking work; requires
  non-trivial state tracking (current row index, dirty-row bitmask); full
  waterfall refresh takes 18 × 2 ms = 36 ms wall time (~28 fps)
- **Dominated by C** if bulk-blit is available: C gives full refresh at ~1 ms
  with no state machine needed

---

## Option C — Bulk blit (pushImage / DMA)

Replace 4,320 individual `drawPixel()` calls with a single `pushImage()` or
`pushImageDMA()` transfer.

### How it works

1. Maintain an off-screen 240 × 18 × 2-byte (RGB565) pixel buffer: **8,640 B**
2. Render all waterfall rows into this buffer (CPU only, fast — no SPI)
3. Push the whole buffer to the display in one SPI transaction:
   - `pushImage()`: blocks for ~0.86 ms (4,320 × 16 bits / 80 MHz SPI)
   - `pushImageDMA()`: returns immediately; SPI hardware transfers while CPU runs

M5Unified is built on LovyanGFX/M5GFX which exposes both APIs. `pushImage()`
is available now; DMA variant requires confirming the display controller is
wired to a DMA-capable SPI bus (almost certain on M5Stack hardware).

### Memory check

- Buffer cost: 240 × 18 × 2 = **8,640 B** contiguous DMA-capable RAM
- Out of 76 KB static headroom: 11% — significant but not blocking
- Must confirm `heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
  ≥ ~16 KB` (buffer + safety margin) at runtime before committing

### Effective poll with pushImage (non-DMA)

```
tx_tick()        ~0 ms
pushImage(...)   ~1 ms  (vs ~43 ms before)
vTaskDelay(2)     2 ms
──────────────────────
Effective: ~3 ms   →   48 % 3 = 0   →   zero jitter   →   PASS
```

With DMA: CPU is free during transfer, so effective poll ≈ 2 ms.

### Trade-off

- **Pro:** solves the root cause (slow primitive); full-rate UI; no threading;
  `if (!g_tx_active)` guard removed — waterfall works during TX
- **Con:** 8.6 KB RAM needed; requires refactoring `ui.cpp` to maintain an
  off-screen buffer; need to verify DMA availability

---

## Option D — Hardware timer isolation (esp_timer)

Move `tx_tick()` into an `esp_timer` 2 ms periodic callback, completely
decoupling tone timing from the UI loop.

### What test_l4 proves

The test validates the architectural invariant: ISOLATED model with `timer_ms=2`
produces `max_late ≤ 2 ms` regardless of `ui_block_ms` (tested up to 100 ms).

### Threading concern

`tx_tick()` shares state with `check_slot_boundary()` and the UI loop:
`g_tx_active`, `g_tx_tone_idx`, `g_tx_next_tone_time`, `g_tx_cancel_requested`,
etc. Calls from two tasks without synchronization = data race.

Required: `portMUX_TYPE` spinlock (or mutex) around every access. Functions
that touch TX state: `tx_tick()`, `check_slot_boundary()`, and several UI reads.

### IRAM constraint

`ESP_TIMER_ISR` dispatch requires the callback in IRAM — **blocked** (0 bytes
remaining). Must use `ESP_TIMER_TASK` (default). The task context allows
ESP_LOG, UART, and all current `tx_tick()` operations. Latency is slightly less
deterministic than ISR mode but adequate for 2 ms period with FreeRTOS
tick at 1 ms.

### Trade-off

- **Pro:** complete architectural decoupling; immune to any future UI blocking;
  test_l4 is the regression harness
- **Con:** threading complexity; mutex wrapping required across many shared-state
  sites; `ESP_TIMER_TASK` jitter is bounded but non-zero (typically < 0.5 ms)
- **No extra RAM** — timer callback overhead is negligible

---

## Decision tree

```
Start: do we need the waterfall to update during TX?
│
├── No (beacon/contest mode is OK frozen) ─────────→ Option A (done)
│                                                      Monitor for regressions
│                                                      with test_l3 + test_l4
│
└── Yes (interactive QSO UX)
    │
    ├── Step 1: probe runtime heap
    │   ESP_LOGI("heap free=%u, largest_dma_block=%u", ...)
    │   after all init in app_main
    │
    ├── largest_dma_block ≥ 16 KB?
    │   ├── Yes ────────────────────────────────────→ Option C (bulk blit)
    │   │                                              Best ROI, no threading
    │   │                                              Confirm pushImageDMA works
    │   │                                              on this SPI bus
    │   │
    │   └── No (heap too fragmented / tight)
    │       ├── Can accept reduced refresh rate? ──→ Option B (chunked)
    │       └── Need full decoupling? ────────────→ Option D (esp_timer)
    │                                                Requires mutex audit
    │
    └── Long-term: C + D (belt and suspenders)
        Option C: waterfall fast enough to not matter
        Option D: tx_tick immune to anything in the UI loop forever
        Regression harness: test_l4 validates D's invariant on every build
```

---

## Recommended path

1. **Ship Option A now.** Waterfall-freeze during TX is not user-visible in
   practice (device is transmitting, not receiving). Fixes the immediate
   hardware ISI bug. Firmware binary is built and ready to flash.

2. **Check runtime heap** on next firmware boot (add one log line). This takes
   5 minutes and determines whether Option C is viable.

3. **Implement Option C** if heap allows it. Refactor `ui_draw_waterfall()` to
   maintain a 240×18 RGB565 off-screen buffer and call `pushImage()` once.
   Remove the `if (!g_tx_active)` guards — waterfall works during TX again.
   Target the DMA variant for zero-CPU-cost transfer.

4. **Add Option D** if/when threading complexity is acceptable and perfect
   timing decoupling is desired (FCC-grade timing accuracy, future protocols
   with shorter symbol periods, etc.). test_l4 is the regression harness — any
   future refactor that breaks ISOLATED invariant will be caught automatically.

---

## Useful commands

```powershell
# Static memory breakdown
cmd /c "C:\esp\v5.5.1\esp-idf\export.bat 2>nul & cd /d ""<project>"" & idf.py size"
cmd /c "C:\esp\v5.5.1\esp-idf\export.bat 2>nul & cd /d ""<project>"" & idf.py size-components"

# Run test suite (from build_vs/)
ctest -C Release --output-on-failure

# Build firmware
cmd /c "C:\esp\v5.5.1\esp-idf\export.bat 2>nul & cd /d ""<project>"" & idf.py build"
```

## Key reference numbers

| Constant | Value | Notes |
|----------|-------|-------|
| FT4 symbol period | 48 ms | Must divide evenly into poll period |
| FT8 symbol period | 160 ms | Much more forgiving |
| Waterfall pixels | 4,320 | 18 rows × 240 columns |
| `drawPixel()` cost | ~10 µs | Per-pixel SPI overhead (estimated) |
| Waterfall blocking (current) | ~43 ms | = 4,320 × 10 µs |
| Target poll period | ≤ 2 ms | 48 % 2 = 0, zero FT4 jitter |
| Bulk-blit cost (`pushImage`) | ~0.86 ms | 4,320 × 16b / 80 MHz SPI |
| Off-screen buffer size | 8,640 B | 240 × 18 × 2 bytes RGB565 |
| DIRAM remaining (static) | 76,469 B | Heap ceiling before init allocs |
| IRAM remaining | 0 B | Full — no new IRAM_ATTR allowed |
