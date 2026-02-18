# ESP8266 Concurrency & WDT Audit (DMD2)

This document audits the ESP8266-specific runtime behavior in this library, with a focus on Wi-Fi coexistence and watchdog (WDT) reset risk.

## Scope

Reviewed files:

- `DMD2_Timer.cpp` (ESP8266 timer setup and ISR path)
- `DMD2.cpp` (scan path and SPI/GPIO work)
- `DMD2.h` / `DMDFrame.cpp` (shared-state and buffer model)

## High-risk issues

### 1) Refresh runs inside timer ISR and performs heavy non-ISR-safe work

`esp8266_ISR_wrapper()` calls `scan_running_dmds()` directly from ISR context, and that calls `BaseDMD::scanDisplay()`. The scan path does:

- many `SPI.transfer()` calls,
- multiple `digitalWrite()` calls,
- potentially `analogWrite()`.

On ESP8266, this is problematic for both latency and safety:

- ISR execution time scales with panel count (`rowsize`) and can exceed its own period.
- Long ISRs starve the SDK background/Wi-Fi work queue and can trigger soft/hard WDT resets.
- Several Arduino-core APIs used here are not designed as IRAM-safe ISR hot-path operations.

**Impact:** Main source of Wi-Fi contention and WDT events under load.

### 2) ISR cadence is very aggressive (250us)

`ESP8266_TIMER0_TICKS` is set to 250us. That is ~4k ISR/s before considering scan duration itself.

For larger chains, scan duration can approach or exceed period, causing near-continuous ISR residency.

**Impact:** CPU starvation for networking/task processing; jitter; WDT sensitivity.

### 3) Concurrency race when (un)registering displays on ESP8266

`register_running_dmd()` can `realloc()` and update `running_dmds/running_dmd_len` while ISR may iterate the same array.

AVR paths disable interrupts around list mutation; ESP8266 begin/end currently do not.

**Impact:** Rare but severe memory corruption/read-after-realloc race (can manifest as crashes/WDT).

### 4) Suspect flash-access guard is effectively a no-op

The ISR wrapper contains:

```cpp
if(((int)0x40200000)) { //Make sure flash isn't being accessed.
  scan_running_dmds();
}
```

This condition is constant-true and does not check flash state.

**Impact:** False sense of safety; ISR still executes flash-resident code/API paths during unsafe windows.

### 5) Shared framebuffer is mutated without synchronization

Drawing APIs mutate `bitmap` while scan reads it concurrently.

This usually causes visual tearing (acceptable for many apps), but on ESP8266 it increases non-determinism and can worsen timing contention during heavy draw loops.

**Impact:** Timing jitter, occasional artifacts; not primary WDT cause but contributes.

## Compatibility-safe improvement plan

The following sequence preserves public API and expected behavior while reducing reset risk.

### Phase 1 (low risk, keep API identical)

1. **Move heavy scan work out of ISR**
   - ISR should only set a `volatile` pending flag/counter and re-arm timer.
   - Add new public helper (non-breaking addition), e.g.:
     - `bool BaseDMD::service();` or `static void BaseDMD::serviceAll();`
   - Call service from `loop()` (or from an optional internal `Ticker` callback that is task-context, not hard ISR).

2. **Protect running list mutation on ESP8266**
   - Wrap `register_running_dmd()` / `unregister_running_dmd()` callsites with interrupt lock (`noInterrupts()/interrupts()` or SDK critical section) on ESP8266.
   - Keep lock window minimal; avoid allocating under lock if possible.

3. **Rate-limit ISR scan work**
   - Add an ESP8266 scan divider (`DMD2_ESP8266_SCAN_DIVIDER`) so not every timer tick performs a full scan.
   - This keeps timer cadence stable while reducing worst-case ISR occupancy.

4. **Raise default refresh period or make it adaptive**
   - Start at 500-1000us default on ESP8266.
   - Optionally skip scans when previous scan overran budget.

5. **Remove invalid flash guard**
   - Replace with real mechanism or delete to avoid misleading behavior.

### Phase 2 (still compatibility-preserving)

1. **Keep SPI path lean for shift-register-driven DMD panels**
   - For this library's primary hardware model (DMD/P10 single-color panels via 16-pin DIL and 74xx logic), SPI is effectively a dedicated bitstream engine, not a multi-device shared peripheral.
   - Prefer a fixed SPI setup and avoid per-scan transaction overhead in the hot path.

2. **Fast GPIO path for default pins on ESP8266**
   - Keep existing public pin API, but when `default_pins==true`, use direct register writes for latch/A/B/noe toggles.
   - Greatly reduces scan CPU cost.

3. **Optional double-buffered swap discipline for ESP8266 users**
   - Keep current APIs; document pattern:
     - draw into off-screen `DMDFrame`, then `swapBuffers()` in short critical section.
   - Minimizes scan/draw overlap and jitter.

### Phase 3 (optional knobs, no breakage)

1. Add opt-in compile-time flags:
   - `DMD2_ESP8266_DEFER_SCAN_FROM_ISR` (default ON)
   - `DMD2_ESP8266_REFRESH_US` (default 500 or 1000)
   - `DMD2_ESP8266_FASTGPIO` (default ON for default pins)

2. Add runtime setter (non-breaking):
   - `setRefreshIntervalUs(uint16_t)`

## Suggested default behavior for ESP8266 (best stability)

- Timer ISR: set pending flag only.
- `loop()` path: `dmd.service()` called frequently.
- Refresh interval: 500-1000us baseline.
- For 2+ panels or active Wi-Fi traffic: prefer 1000us and fast GPIO path.

## Notes on preserving compatibility

- Keep `begin()/end()` signatures and semantics.
- Keep current constructor/default pins and existing examples working.
- New methods/flags should be additive and optional.
- Existing sketches that do nothing extra should still refresh display (via default internal service strategy).

## Quick triage checklist for current users (without code changes)

1. Reduce panel count or brightness (`setBrightness`) to reduce scan duty.
2. Avoid heavy draw loops without delays/yields in user code.
3. Prefer manual `beginNoTimer()+scanDisplay()` scheduling in `loop()` if Wi-Fi stability is critical.
4. Increase inter-scan interval manually in user scheduling.

---

If you want, next step can be a minimal patch set that implements **Phase 1 only** behind compile-time flags so behavior remains backward compatible but safer by default on ESP8266.
