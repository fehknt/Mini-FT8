# Build and Test Guide

Reference for agents and developers working on Mini-FT8.  
Covers: firmware build, flashing, and the tx_e2e test suite.

---

## Repository layout

```
Mini-FT8/
├── main/                        # ESP32 application (main.cpp, radio_control_qmx.cpp, …)
├── components/ft8_lib/          # FT8/FT4 codec (shared by firmware and tests)
├── build/                       # IDF build output (generated)
│   └── MiniFT8_Merged_Auto.bin  # Single flashable image (bootloader + app + spiffs)
├── release/                     # Manually-committed release binaries (V1.x, V2.x)
├── tests/tx_e2e/                # Host-side unit / integration tests (CMake + ctest)
│   ├── golden/                  # Committed WAV files for golden_rx test
│   └── build_vs/                # MSVC build output (generated)
└── docs/                        # Architecture and reference docs (this file lives here)
```

---

## 1. Firmware build (ESP-IDF, targets ESP32-S3)

### Toolchain location

The project uses **ESP-IDF v5.5.1** installed by the Espressif IDE Manager (eim).

| Item | Path |
|------|------|
| IDF source | `C:\esp\v5.5.1\esp-idf` |
| Toolchain (xtensa) | `C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin` |
| Python venv | `C:\Espressif\tools\python\v5.5.1\venv\Scripts` |
| CMake | `C:\Espressif\tools\cmake\3.30.2\bin` |
| Ninja | `C:\Espressif\tools\ninja\1.12.1` |
| IDF tools root | `C:\Espressif\tools` |

The PowerShell profile that sets all of this up is at:  
`C:\Espressif\tools\Microsoft.v5.5.1.PowerShell_profile.ps1`

### Building from PowerShell (without the profile loaded)

```powershell
$env:IDF_PATH            = "C:\esp\v5.5.1\esp-idf"
$env:IDF_TOOLS_PATH      = "C:\Espressif\tools"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v5.5.1\venv"
$env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://C:\Espressif\tools"
$env:PATH = "C:\Espressif\tools\ccache\4.11.2\ccache-4.11.2-windows-x86_64;" +
            "C:\Espressif\tools\cmake\3.30.2\bin;" +
            "C:\Espressif\tools\ninja\1.12.1\;" +
            "C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;" +
            "C:\Espressif\tools\python\v5.5.1\venv\Scripts;" +
            $env:PATH

Set-Location "C:\Users\fehkn\OneDrive\Documents\github repos\mini-ft8\Mini-FT8"

& "C:\Espressif\tools\python\v5.5.1\venv\Scripts\python.exe" `
    "C:\esp\v5.5.1\esp-idf\tools\idf.py" build
```

### Building from the IDF PowerShell environment

If you open the **ESP-IDF v5.5 PowerShell** shortcut (created by eim), the environment is already configured:

```powershell
cd "C:\Users\fehkn\OneDrive\Documents\github repos\mini-ft8\Mini-FT8"
idf.py build
```

### Build output

A successful build prints:
```
Project build complete.
Wrote 0x1xxxxx bytes to file MiniFT8_Merged_Auto.bin, ready to flash to offset 0x0
```

The merged binary is at:
```
build\MiniFT8_Merged_Auto.bin
```

**Always verify the timestamp of `MiniFT8_Merged_Auto.bin` after making changes to `main/`.  
The test suite (Section 2) builds a separate host binary — passing tests do NOT update the firmware binary.**

### Flashing

```powershell
# Replace COM3 with the actual port
& "C:\Espressif\tools\python\v5.5.1\venv\Scripts\python.exe" `
    "C:\esp\v5.5.1\esp-idf\tools\idf.py" -p COM3 flash
```

Or use the full esptool command printed at the end of `idf.py build`.

---

## 2. tx_e2e test suite (host-side, MSVC)

Located at `tests/tx_e2e/`.  Build system: CMake + Visual Studio 2022 (or later).  
Build output: `tests/tx_e2e/build_vs/`.

### One-time CMake configure

```cmd
cd tests\tx_e2e
mkdir build_vs
cd build_vs
cmake .. -G "Visual Studio 18 2026"
```

> **Note:** The installed generator is Visual Studio 18 2026 (not 2022).  
> If you see a generator mismatch error, delete `build_vs\CMakeCache.txt` **and** any
> `CMakeCache.txt` that landed in the source directory (`tests/tx_e2e/CMakeCache.txt`),
> then reconfigure.  Do not leave a `CMakeCache.txt` in the source tree — it overrides
> the build-directory cache and causes confusing "wrong generator" errors.

### Build all targets

```cmd
cd tests\tx_e2e\build_vs
cmake --build . --config Release
```

### Run all tests

```cmd
ctest -C Release --output-on-failure
```

Expected output:
```
1/5 Test #1: simple           ...  Passed
2/5 Test #2: l1_encoder       ...  Passed
3/5 Test #3: l2_state_machine ...  Passed
4/5 Test #4: golden_rx        ...  Passed
5/5 Test #5: ta_format        ...  Passed

100% tests passed, 0 tests failed out of 5
```

### Test descriptions

| ctest name | Binary | What it validates |
|------------|--------|-------------------|
| `simple` | `test_simple` | ft8_lib sanity (encode + decode a single message) |
| `l1_encoder` | `test_l1` | Layer 1: encode→synth→decode round-trip for 18 FT8/FT4 cases; writes fail WAV on decode failure |
| `l2_state_machine` | `test_l2` | Layer 2: slot-anchor timing + encode→synth→decode for 14 cases (FT8 + FT4, various delays, skip counts, edge frequencies); writes fail WAV on decode failure |
| `golden_rx` | `test_golden_rx` | Decodes 7 committed WAV files from `tests/tx_e2e/golden/`; validates decoder in isolation — independent of encoder |
| `ta_format` | `test_ta_format` | Validates QMX TA command string formatting for all FT8/FT4 tone values at 300/1500/2700 Hz base; asserts `ta_frac >= 0` and round-trip accuracy ≤ 0.01 Hz |

### Golden WAV files

The `golden/` directory contains 7 committed 16-bit mono 6 kHz WAV files (4 FT8, 3 FT4).  
See `golden/MANIFEST.txt` for the full listing.

Regenerate them only after an intentional change to the encoder, synth, or sample rate:

```cmd
cd tests\tx_e2e\build_vs
.\Release\gen_golden.exe ..\golden
```

Then commit the updated WAV files.  The `golden_rx` test will fail until they are regenerated if the encoder output changes.

### Fail WAV dumps

`test_l1` and `test_l2` write a WAV file to the current working directory (or `FAIL_WAV_DIR` if defined) whenever a decode fails.  File names are `fail_l1_NN.wav` / `fail_l2_NN.wav`.  Open in Audacity or pass through `jt9` for offline diagnosis.

---

## 3. Known bugs fixed (do not revert)

### TA command negative fractional part (`radio_control_qmx.cpp`, `main.cpp`)

**Symptom:** QMX receives malformed commands like `TA1521.-17;` for certain tone frequencies.  
**Root cause:** `lrintf(tone_hz)` rounds to nearest; for `.75` or `.8333` Hz fractions it rounds `ta_int` up, making `frac` negative.  
**Fix:** Use `floorf(tone_hz)` so `ta_int` always truncates down and `frac ∈ [0, 1)`.  
**Affected tones:** FT8 tones 3, 6, 7 and FT4 tones 1, 2 at every base frequency.  
**Test:** `ta_format` ctest demonstrates the old bug (Part 1) and validates the fix (Part 2).

### FT4 symbol period truncation — 47 ms instead of 48 ms (`stream_uac.cpp`, `stream_mic.cpp`)

**Symptom:** FT4 transmits each symbol ~1 ms too short, causing inter-symbol interference (ISI).
Recordings show Costas sync scores of ~10–13 (vs ~31 for a clean signal) and LDPC errors that
plateau at 6–20 regardless of iteration count — the decode never converges.  
**Root cause:** `(uint32_t)(g_protocol->symbol_period * 1000.0f)` — `0.048f` is not exactly
representable in IEEE-754 single precision; the product evaluates to `0.047999...`, which
truncates to 47 on cast to `uint32_t`.  
**Fix:** `(uint32_t)lrintf(g_protocol->symbol_period * 1000.0f)` — rounds to nearest integer
before the cast, giving the correct 48 ms.  
**Locations fixed:**
- `main/stream_uac.cpp` line ~706 (`vTaskDelayUntil` in the UAC playback loop)
- `main/stream_mic.cpp` line ~169 (`vTaskDelayUntil` in the microphone TX loop)  

**Verification:** After flashing, re-record a transmission into WSJT-X. A clean FT4 signal should
decode with LDPC errors = 0 and a Costas score ≥ 28. Use `test_wav_diag` to inspect recordings.

---

## 4. Hardware testing notes

- **Target board:** M5Cardputer (ESP32-S3)
- **Radio interface:** QMX transceiver via UART CAT at 38400 baud
- **FT4 trigger window:** 500 ms (vs FT8's 4000 ms). If the main loop stalls under load (decode + display), FT4 TX can be silently skipped. Watch for missed-trigger log lines.
- **UART back-pressure:** FT4 sends 105 TA commands at ~20 Hz. Not stress-tested in software. Monitor for late or dropped tones on first hardware run.
- **Verify binary timestamp** before flashing — `build\MiniFT8_Merged_Auto.bin` must be newer than the last `main/` change.
