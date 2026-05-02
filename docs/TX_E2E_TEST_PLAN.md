# FT4/FT8 TX Modulation E2E Test — Implementation Plan

## 1. Goal

Verify that the firmware's TX path emits a tone sequence that can be decoded by a standard FT4/FT8 decoder, for both protocols, across the full set of message types. This catches encoding bugs, timing math errors, and tone-spacing regressions before they require an over-the-air smoke test with a radio.

**Out of scope** (separate tests):
- USB CDC latency / hardware jitter
- QMX firmware response to TA commands
- RF-level modulation quality (Gaussian shaping, IMD)

## 2. Approach

Run the firmware's encoder + TX state machine **on the host PC** (Linux/macOS/Windows), feed the output through a synthesized audio pipeline, then decode with `ft8_lib`'s reference decoder. Compare decoded text vs. input text.

Two layers of testing, in order of value:

| Layer | What it tests | Effort |
|-------|---------------|--------|
| **L1: Encoder-only** | `ftx_message_encode` + `ft8_encode`/`ft4_encode` produce decodable tones | Low |
| **L2: Full TX state machine** | `tx_start` + `tx_tick` produce decodable tones with correct timing | Medium |

L2 catches bugs that L1 cannot — e.g. wrong tone spacing in `tx_tick`, off-by-one in `g_tx_tone_idx`, slot anchor math regressions.

## 3. Host build setup

`ft8_lib` (in `components/ft8_lib/`) is portable C and already has a `CMakeLists.txt` that works on the host. The test will live in a new top-level `tests/tx_e2e/` directory and build independently from the ESP32 firmware.

```
Mini-FT8/
└── tests/
    └── tx_e2e/
        ├── CMakeLists.txt
        ├── README.md
        ├── synth.c              # tone-array → PCM
        ├── synth.h
        ├── cat_recorder.cpp     # mock CAT layer (L2 only)
        ├── cat_recorder.h
        ├── tx_state_machine.cpp # extracted tx_start + tx_tick logic (L2)
        ├── test_l1_encoder.cpp
        ├── test_l2_state_machine.cpp
        └── golden/              # known-good reference WAVs (optional)
```

`CMakeLists.txt` should:
- Add `components/ft8_lib` as a subdirectory (it builds as a static lib on host).
- Compile our test sources and link against `ft8_lib`.
- Output two executables: `test_l1` and `test_l2`.

```cmake
cmake_minimum_required(VERSION 3.16)
project(tx_e2e C CXX)
set(CMAKE_CXX_STANDARD 17)

# ft8_lib's existing CMakeLists builds as a host library
add_subdirectory(${CMAKE_SOURCE_DIR}/../../components/ft8_lib ft8_lib)

add_executable(test_l1 test_l1_encoder.cpp synth.c)
target_link_libraries(test_l1 PRIVATE ft8_lib m)

add_executable(test_l2 test_l2_state_machine.cpp tx_state_machine.cpp
               cat_recorder.cpp synth.c)
target_link_libraries(test_l2 PRIVATE ft8_lib m)

enable_testing()
add_test(NAME l1_encoder COMMAND test_l1)
add_test(NAME l2_state_machine COMMAND test_l2)
```

Build and run:
```
cd tests/tx_e2e && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

## 4. Layer 1 — Encoder-only test

### Pipeline

```
text → ftx_message_encode → payload(77 bits) → ft8_encode/ft4_encode → tones[NN]
  → synth → PCM samples → monitor + decode → decoded text → compare
```

### `synth.c` — tone array to PCM

Synthesize `nn` tones at sample rate `fs` (use 12000 Hz to match decoder defaults). Each tone is a constant-amplitude sinusoid at `base_hz + spacing_hz * tones[i]` for `symbol_period` seconds. Maintain phase continuity across symbol boundaries (otherwise decoder confidence drops).

```c
// synth.h
void synth_fsk(const uint8_t* tones, int nn,
               float symbol_period_s, float base_hz, float spacing_hz,
               int sample_rate, float amplitude,
               float* out_samples, int* out_len);
```

```c
// pseudocode
void synth_fsk(...) {
    int samples_per_symbol = (int)round(symbol_period_s * sample_rate);
    double phase = 0.0;
    int idx = 0;
    for (int i = 0; i < nn; i++) {
        double freq = base_hz + spacing_hz * tones[i];
        double phase_inc = 2.0 * M_PI * freq / sample_rate;
        for (int s = 0; s < samples_per_symbol; s++) {
            out_samples[idx++] = amplitude * sin(phase);
            phase += phase_inc;
            if (phase > 2*M_PI) phase -= 2*M_PI;  // keep bounded
        }
    }
    *out_len = idx;
}
```

Notes for the junior engineer:
- **Phase continuity matters.** Don't restart the sine wave at each symbol — the decoder's correlator weighs phase coherence.
- **Add a small buffer of silence** before/after the tones (e.g. 0.5 s on each side) so the decoder's framing has some headroom.
- **Keep amplitude well below 1.0** (e.g. 0.5) so quantization or any added noise doesn't clip.

### Decode helper

`ft8_lib`'s `monitor_t` plus `ftx_find_candidates` + `ftx_decode_candidate` is the reference path. Easier: copy the loop from `decode_ft8.c` (the example program in `components/ft8_lib/decode_ft8.c`) and trim it to:

```cpp
// pseudocode
DecodeResult decode_pcm(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto) {
    monitor_config_t cfg = { .f_min=200, .f_max=2900, .sample_rate=fs,
                             .time_osr=2, .freq_osr=2, .protocol=proto };
    monitor_t mon; monitor_init(&mon, &cfg); monitor_reset(&mon);

    // Process in blocks
    int blk = mon.block_size;
    for (int i = 0; i + blk <= n_samples; i += blk)
        monitor_process(&mon, samples + i);

    // Find candidates and decode
    ftx_candidate_t cands[20];
    int nc = ftx_find_candidates(&mon.wf, 20, cands, kMin_score);

    DecodeResult r{};
    for (int i = 0; i < nc; i++) {
        ftx_message_t msg;
        ftx_decode_status_t st;
        if (ftx_decode_candidate(&mon.wf, &cands[i], 4, &msg, &st)) {
            char text[60];
            ftx_message_decode(&msg, &hash_if, text);
            r.found = true;
            strncpy(r.text, text, sizeof(r.text));
            r.snr = cands[i].score;
            break;
        }
    }
    monitor_free(&mon);
    return r;
}
```

The hash interface (`hash_if`) for the decoder needs a noop implementation: return false on lookup, do nothing on save.

### Test cases

```cpp
struct Case {
    const char* text;
    ftx_protocol_t proto;
    float base_hz;
};

const Case CASES[] = {
    // Basic CQ
    {"CQ TEST FN42",        FTX_PROTOCOL_FT8, 1500.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT4, 1500.0f},
    // Full QSO sequence (TX1..TX5)
    {"W1ABC K9XYZ FN42",    FTX_PROTOCOL_FT8, 1500.0f},
    {"W1ABC K9XYZ -12",     FTX_PROTOCOL_FT8, 1500.0f},
    {"W1ABC K9XYZ R-08",    FTX_PROTOCOL_FT8, 1500.0f},
    {"W1ABC K9XYZ RR73",    FTX_PROTOCOL_FT8, 1500.0f},
    {"W1ABC K9XYZ 73",      FTX_PROTOCOL_FT8, 1500.0f},
    // All of the above for FT4 too
    // Edge offsets
    {"CQ TEST FN42",        FTX_PROTOCOL_FT8,  300.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT8, 2700.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT4,  300.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT4, 2700.0f},
    // Free text
    {"HELLO WORLD",         FTX_PROTOCOL_FT8, 1500.0f},
    {"73 GL",               FTX_PROTOCOL_FT4, 1500.0f},
};
```

For each case: encode → synth → decode → assert `decoded.text == case.text` (after WSJT-style normalization — see §7).

### Pseudocode of the test driver

```cpp
int main() {
    int passed = 0, failed = 0;
    for (auto& c : CASES) {
        ftx_message_t msg;
        if (ftx_message_encode(&msg, &hash_if, c.text) != FTX_MESSAGE_RC_OK) {
            FAIL("encode failed: " << c.text);
            continue;
        }
        uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
        int nn;
        float sym_period, spacing;
        if (c.proto == FTX_PROTOCOL_FT8) {
            ft8_encode(msg.payload, tones);
            nn = FT8_NN; sym_period = FT8_SYMBOL_PERIOD; spacing = 6.25f;
        } else {
            ft4_encode(msg.payload, tones);
            nn = FT4_NN; sym_period = FT4_SYMBOL_PERIOD; spacing = 20.8333f;
        }
        std::vector<float> pcm(/* nn * sym_period * fs + padding */);
        synth_fsk(tones, nn, sym_period, c.base_hz, spacing,
                  12000, 0.5f, pcm.data(), &n_samples);
        DecodeResult r = decode_pcm(pcm.data(), n_samples, 12000, c.proto);
        if (r.found && normalize(r.text) == normalize(c.text)) {
            passed++;
        } else {
            failed++;
            printf("FAIL [%s] '%s' decoded='%s'\n",
                   c.proto==FTX_PROTOCOL_FT4?"FT4":"FT8",
                   c.text, r.found ? r.text : "<none>");
        }
    }
    printf("L1: %d/%d passed\n", passed, passed+failed);
    return failed ? 1 : 0;
}
```

## 5. Layer 2 — Full TX state machine test

This is the higher-value test. It exercises `tx_start` and `tx_tick` exactly as they run on device, and feeds their **CAT command output** into the synthesizer. It catches bugs that L1 misses, e.g. tone spacing applied differently in `tx_start` vs `tx_tick`, or incorrect symbol period math.

### Extracting the state machine

`tx_start` and `tx_tick` in `main/main.cpp` reference dozens of ESP-IDF / global-state symbols (autoseq, M5, FreeRTOS, `radio_control_*`, etc.). We need to extract just the tone-emission logic into a host-buildable module.

Create `tests/tx_e2e/tx_state_machine.cpp` with stripped-down versions:

```cpp
// tx_state_machine.h
struct TxConfig {
    const ProtocolConfig* protocol;
    int base_hz;
    int64_t slot_start_ms;
    int skip_tones;
    const char* text;
};

// Drives tx_start + simulated tx_tick; returns CAT command stream.
std::vector<CatEvent> run_tx(const TxConfig& cfg, int64_t loop_delay_ms = 2);
```

```cpp
// CatEvent represents one TA command at a wall-clock time.
struct CatEvent {
    int64_t time_ms;       // simulated wall clock
    enum { TX_BEGIN, TA, TX_END } kind;
    float tone_hz;         // valid for TA
};
```

The implementation copies the timing math exactly from `main.cpp`:

```cpp
// pseudocode — must mirror main.cpp tx_start/tx_tick byte for byte
std::vector<CatEvent> run_tx(const TxConfig& cfg, int64_t loop_delay_ms) {
    std::vector<CatEvent> events;
    int64_t now = cfg.slot_start_ms;  // simulated clock starts at slot boundary

    // === tx_start ===
    ftx_message_t msg;
    ftx_message_encode(&msg, &hash_if, cfg.text);
    uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
    if (cfg.protocol->protocol_id == FTX_PROTOCOL_FT4)
        ft4_encode(msg.payload, tones);
    else
        ft8_encode(msg.payload, tones);

    int sym_ms = (int)(cfg.protocol->symbol_period * 1000.0f);
    int tone_idx = (cfg.skip_tones >= cfg.protocol->total_symbols)
                   ? cfg.protocol->total_symbols : cfg.skip_tones;
    int64_t next_tone_time = cfg.slot_start_ms + tone_idx * sym_ms;

    events.push_back({now, CatEvent::TX_BEGIN, 0.0f});
    // first TA from tx_start
    if (tone_idx < cfg.protocol->total_symbols) {
        float hz = cfg.base_hz + cfg.protocol->tone_spacing * tones[tone_idx];
        events.push_back({now, CatEvent::TA, hz});
    }
    int last_ta_int = -1, last_ta_frac = -1;
    auto record_ta = [&](float hz) {
        // mirror tx_send_ta dedup
        int ta_int = (int)lrintf(hz);
        int ta_frac = (int)lrintf((hz - ta_int) * 100.0f);
        if (ta_int == last_ta_int && ta_frac == last_ta_frac) return;
        last_ta_int = ta_int; last_ta_frac = ta_frac;
        events.push_back({now, CatEvent::TA, hz});
    };

    // === main loop running tx_tick ===
    while (tone_idx < cfg.protocol->total_symbols) {
        if (now < next_tone_time) {
            now += loop_delay_ms;
            continue;
        }
        float hz = cfg.base_hz + cfg.protocol->tone_spacing * tones[tone_idx];
        record_ta(hz);
        tone_idx++;
        next_tone_time = cfg.slot_start_ms + tone_idx * sym_ms;
    }
    events.push_back({now, CatEvent::TX_END, 0.0f});
    return events;
}
```

### CAT-event → audio synthesis

A radio receiving CAT commands produces audio at the most-recently-set TA frequency until the next TA. So events at `(t0, TA=f0)`, `(t1, TA=f1)`, ... `(tN, TX_END)` produce:
- `t0..t1`: tone at `f0`
- `t1..t2`: tone at `f1`
- ...
- `t_{N-1}..tN`: tone at `f_{N-1}`

Pre-`TX_BEGIN` and post-`TX_END`: silence.

```cpp
std::vector<float> events_to_pcm(const std::vector<CatEvent>& events,
                                 int sample_rate, float amplitude) {
    int64_t t_start = events.front().time_ms;
    int64_t t_end = events.back().time_ms;
    int64_t total_ms = t_end - t_start + 500;  // tail silence
    int total_samples = (int)((double)total_ms * sample_rate / 1000.0);
    std::vector<float> pcm(total_samples, 0.0f);

    double phase = 0.0;
    float current_hz = 0.0f;
    bool tx_active = false;

    int evt = 0;
    for (int i = 0; i < total_samples; i++) {
        int64_t t_ms = t_start + (int64_t)((double)i * 1000.0 / sample_rate);
        // advance event pointer
        while (evt < events.size() && events[evt].time_ms <= t_ms) {
            switch (events[evt].kind) {
                case CatEvent::TX_BEGIN: tx_active = true; break;
                case CatEvent::TA: current_hz = events[evt].tone_hz; break;
                case CatEvent::TX_END: tx_active = false; break;
            }
            evt++;
        }
        if (tx_active && current_hz > 0) {
            pcm[i] = amplitude * sin(phase);
            phase += 2.0 * M_PI * current_hz / sample_rate;
            if (phase > 2*M_PI) phase -= 2*M_PI;
        }
    }
    return pcm;
}
```

**Phase continuity caveat:** when `current_hz` changes, the phase accumulator carries forward — same as a real DDS. Don't reset phase to 0.

### L2 test cases

Same matrix as L1, plus:

| Variation | Why |
|-----------|-----|
| `skip_tones = 0, 1, 5, 10` | Verify late-start path emits correctly aligned tones |
| `loop_delay_ms = 1, 2, 5` | Confirm output is independent of loop pacing |
| `slot_start_ms = 0, 7500, 15000, 99999000` | Confirm slot anchor math holds for large times |

Pass criterion: decode succeeds and decoded text matches input.

### Pseudocode test driver

```cpp
int main() {
    int passed = 0, failed = 0;
    for (auto& c : L2_CASES) {
        TxConfig cfg = build_cfg(c);
        auto events = run_tx(cfg, c.loop_delay_ms);
        auto pcm = events_to_pcm(events, 12000, 0.5f);
        DecodeResult r = decode_pcm(pcm.data(), pcm.size(), 12000, cfg.protocol->protocol_id);
        if (r.found && normalize(r.text) == normalize(c.text))
            passed++;
        else { failed++; print_failure(c, r, events); }
    }
    return failed ? 1 : 0;
}
```

When a case fails, dump the events and the PCM to disk (`fail_<idx>.wav`, `fail_<idx>.events`) so the engineer can inspect them in Audacity / WSJT-X.

## 6. Pass/fail criteria

- **Pass**: every case in §4.4 and §5.4 decodes, and the normalized decoded text matches the normalized input.
- **Normalization** (call this `normalize(s)`):
  - Uppercase
  - Collapse whitespace runs to single space
  - Strip leading/trailing whitespace
  - Some legitimate variations the encoder may emit:
    - `<...>` hashed callsigns: skip cases that exercise these initially
    - SNR encoding: WSJT formats `-08` as `-08` not `-8`; treat strings as raw
- **CI exit code**: `0` for all-pass, `1` otherwise.
- **Per-case logs** to `build/Testing/Temporary/LastTest.log` via ctest.

## 7. Known pitfalls and gotchas

The junior engineer should read this section before debugging.

1. **Hashed callsigns.** `ftx_message_encode` may produce `<W1ABC>` for callsigns the decoder doesn't have in its hash table. Provide a stub `ftx_callsign_hash_interface_t` that records hashes during encode and returns them during decode. Otherwise the decoded text will use `<...>` placeholders and the comparison will fail.

   ```c
   // Stub: hash table = static array of (hash22, callsign) tuples written by encode, read by decode.
   ```

2. **Sample rate.** `ft8_lib`'s decoder has been most heavily exercised at 12000 Hz. Use 12000 Hz throughout the test. Don't try 6000 Hz unless you're explicitly testing the firmware's downsampling path.

3. **Decoder window padding.** Add at least 0.5 s of silence before the first tone. The decoder needs ~half a symbol of pre-context to find the first Costas array.

4. **Time OSR / Freq OSR.** Use `time_osr=2, freq_osr=2` — same as the firmware. With `1,1` the decoder is much weaker and you'll see false negatives that aren't real bugs.

5. **Protocol ID enum value.** `FTX_PROTOCOL_FT8` and `FTX_PROTOCOL_FT4` come from `ft8_lib/ft8/constants.h`. Both must be passed correctly to `monitor_init` — passing FT8 to a FT4 signal returns 0 candidates and looks like an encode bug.

6. **ftx_message_decode signature.** Current ft8_lib API uses `ftx_message_decode(&msg, &hash_if, char* text)`. Check the actual signature in `ft8/message.h` since this varies between forks.

7. **Compiler warnings.** Build with `-Wall -Wextra -Werror`. The synth and decode pseudocode here drops some const-ness for brevity — clean it up.

## 8. Recommended development order

1. **First:** stand up the host build, get a hello-world that calls `ftx_message_encode` and prints the payload bytes. Confirm `ft8_lib` links cleanly on host.
2. **Then:** L1 with one case (`"CQ TEST FN42"` FT8 at 1500 Hz). Verify it decodes. This validates `synth_fsk` and the decoder wrapper.
3. **Then:** expand L1 to the full case matrix. Fix `normalize` until everything passes.
4. **Then:** extract the state machine for L2. Start with one case, `skip_tones=0`. Cross-check the event stream against the firmware logs from `tx_start`/`tx_tick` if you can capture them.
5. **Then:** expand L2 to the full matrix.
6. **Finally:** wire into CI (`ctest`) and document failure-debug workflow in the README.

## 9. Future extensions (not in scope, but worth noting)

- **Add Gaussian noise** to the synthesized PCM and verify decode threshold matches FT4/FT8 spec (~ −20 dB SNR for FT8, −17.5 dB for FT4).
- **Capture real device output** by intercepting `qmx_send_cmd` on hardware and dumping it over UART; replay through `events_to_pcm` and decode.
- **WSJT-X compatibility test:** save the synthesized PCM as a `.wav`, decode it with the actual `wsjtx_app` `jt9` binary, assert the same result. This is the gold standard but requires installing WSJT-X on CI.
- **Time-domain assertion:** independently verify that consecutive TA events are exactly `symbol_period_ms` apart (within ±loop_delay_ms tolerance).

## 10. Deliverables checklist

- [ ] `tests/tx_e2e/CMakeLists.txt`
- [ ] `tests/tx_e2e/synth.{c,h}` with phase-continuous FSK
- [ ] `tests/tx_e2e/decode_helper.{cpp,h}` with hashed-callsign stub
- [ ] `tests/tx_e2e/test_l1_encoder.cpp` and a passing L1 run
- [ ] `tests/tx_e2e/tx_state_machine.{cpp,h}` extracted from `main.cpp`
- [ ] `tests/tx_e2e/cat_recorder.{cpp,h}` (CatEvent type + recorder)
- [ ] `tests/tx_e2e/test_l2_state_machine.cpp` and a passing L2 run
- [ ] `tests/tx_e2e/README.md`: how to build/run, what failures mean, how to dump WAVs
- [ ] CI integration via `ctest`

When this is done, any future change to encoding, tone spacing, slot timing, or the TX state machine will fail fast without needing a radio in the loop.
