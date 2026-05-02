# FT4/FT8 TX Modulation E2E Test Suite

This directory contains the implementation of the E2E test plan for validating FT4/FT8 transmit signal modulation. See `../docs/TX_E2E_TEST_PLAN.md` for the full test plan.

## Implementation Status

### Completed ✓
- **CMakeLists.txt**: Host build configuration for ft8_lib portability
- **synth.c/synth.h**: Phase-continuous FSK tone synthesis with parameterized tone spacing
- **decode_helper.h/.cpp**: Decoder wrapper with hash table stub and monitor initialization
- **test_simple.cpp**: Diagnostic test verifying message encoding works
- **test_synth.cpp**: Diagnostic test verifying tone synthesis works
- **MSVC/Windows compatibility fixes**:
  - VLA fix in components/ft8_lib/ft8/decode.c (malloc instead of stack allocation)
  - Math library (m.lib) linked only on non-Windows
  - stpcpy_compat.c linked for Windows compatibility
  - M_PI definition for MSVC

### Partially Complete ⚠️
- **test_l1_encoder.cpp**: L1 test cases defined, but hangs in decode stage
  - Message encoding: ✓ works
  - Tone synthesis: ✓ works
  - PCM decoding: ❌ hangs (likely in monitor_init or monitor_process)

### Not Yet Implemented ❌
- **Layer 2 (L2) state machine tests**: TX state extraction and timing validation
- **cat_recorder**: CAT event recording and replay
- **README.md**: Developer guide (this file - partial)

## Building

### On Windows (MSVC)
```powershell
cd tests\tx_e2e
cmake -B build_vs -G "Visual Studio 18 2026" -A x64
cmake --build build_vs --config Release
```

### On Linux/macOS (GCC)
```bash
cd tests/tx_e2e
mkdir build && cd build
cmake .. -G Ninja   # or "Unix Makefiles"
cmake --build .
```

## Running Tests

```bash
# Run simple diagnostic test
./build_vs/Release/test_simple.exe

# Run synthesis test
./build_vs/Release/test_synth.exe

# Run L1 encoder test (currently hangs)
./build_vs/Release/test_l1.exe
```

## Known Issues

1. **test_l1_encoder hangs in decode stage**
   - Likely in `monitor_init` or `monitor_process`
   - The hang occurs AFTER successful message encoding and tone synthesis
   - Diagnostic: message "decode_pcm: n_samples=..." is never printed to stderr
   - Needs investigation of monitor_t initialization or FFT setup

2. **MSVC-specific warnings**
   - Disable C4996 warnings about unsafe string functions (_CRT_SECURE_NO_WARNINGS)
   - Several cast-related warnings in ft8_lib due to size_t conversions

## Architecture

### Phase 1: Encoder-only (L1)
- Input: text message
- `ftx_message_encode` → payload
- `ft8_encode`/`ft4_encode` → tone indices
- `synth_fsk` → PCM samples
- `monitor_t` + `ftx_find_candidates` + `ftx_decode_candidate` → decoded text
- Compare input vs. output

### Phase 2: State machine (L2) - not yet implemented
- Simulate TX timing: `tx_start` + repeated `tx_tick`
- Record CAT commands (TA commands + timing)
- Synthesize PCM from CAT command stream
- Decode and verify

## Development Notes

### Why malloc() instead of VLAs?
MSVC doesn't support C99 Variable Length Arrays (VLA). Changed `ft8_decode_multi_symbols` to use `malloc()`/`free()` for the temporary `s2[]` buffer. The maximum buffer is ~4KB so performance impact is minimal.

### Hash Table Stub
The decoder needs to look up callsigns from hash values. For testing, we use a simple map that:
- Records hashes during encode (hash_save_stub)
- Returns those same hashes during decode (hash_lookup_stub)
- Fallback to return false (default behavior) for non-test callsigns

###  Normalize Function
Message comparison is case-insensitive and whitespace-insensitive. The normalize_text function:
- Converts to uppercase
- Collapses multiple spaces to single space
- Strips leading/trailing whitespace
- Handles <HASH> notation for non-standard callsigns

## Next Steps

1. **Debug monitor_init hang**
   - Add fputs/fflush debug output to decode_helper.cpp line-by-line
   - Check if the issue is FFT initialization, waterfall allocation, or configuration
   - Consider using smaller test inputs to reduce FFT computation

2. **Implement L2 state machine**
   - Extract tx_start/tx_tick from main/main.cpp
   - Create cat_recorder.cpp for CAT event replay
   - Create test_l2_state_machine.cpp with timing-sensitive test cases

3. **Add to CI**
   - Wire into ctest
   - Store golden test results
   - Generate failure reports with WAV files for manual inspection

## Files Reference

| File | Purpose | Status |
|------|---------|--------|
| CMakeLists.txt | Build config | ✓ working |
| synth.c/h | FSK synthesis | ✓ working |
| decode_helper.cpp/h | Monitor + decode wrapper | ⚠️ hangs in monitor_init |
| test_simple.cpp | Encode diagnostic | ✓ working |
| test_synth.cpp | Synthesis diagnostic | ✓ working |
| test_l1_encoder.cpp | Full L1 test suite | ⚠️ hangs in decode |
| README.md | This file | ⚠️ in progress |

