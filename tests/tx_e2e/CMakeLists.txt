cmake_minimum_required(VERSION 3.16)
project(tx_e2e C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

# MSVC needs special handling for VLAs and C99 features
if(MSVC)
    # Disable warnings for unsafe string functions
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Allow VLAs by using /permissive- (disable conformance mode for this feature)
    add_compile_options(/permissive-)
endif()

# Compiler flags (platform-specific)
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /WX-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /WX-")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-format")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
endif()

# List of ft8_lib source files (non-IDF compatible version)
set(FT8_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/decode.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/message.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/text.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/encode.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/constants.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/crc.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/ft8/ldpc.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/fft/kiss_fft.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/fft/kiss_fftr.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/common/monitor.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/common/fft_wrapper.c
    ${CMAKE_SOURCE_DIR}/../../components/ft8_lib/common/stpcpy_compat.c
)

# Create ft8_lib static library
add_library(ft8_lib STATIC ${FT8_LIB_SOURCES})
target_include_directories(ft8_lib PUBLIC ${CMAKE_SOURCE_DIR}/../../components/ft8_lib)
if(NOT MSVC)
    target_compile_options(ft8_lib PRIVATE
        -Wno-unused-const-variable
        -Wno-format
        -Wno-unused-variable
    )
endif()

# Link against math library (only on non-Windows systems)
if(NOT WIN32)
    target_link_libraries(ft8_lib PUBLIC m)
endif()

# L1 Encoder Test
add_executable(test_l1
    test_l1_encoder.cpp
    decode_helper.cpp
    synth.c
)
target_include_directories(test_l1 PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_l1 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l1 PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(test_l1 PRIVATE -Wno-format)
endif()

# Simple diagnostic test
add_executable(test_simple test_simple.cpp)
target_include_directories(test_simple PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_simple PRIVATE ft8_lib)
else()
    target_link_libraries(test_simple PRIVATE ft8_lib m)
endif()

# Synth test
add_executable(test_synth test_synth.cpp synth.c)
target_include_directories(test_synth PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_synth PRIVATE ft8_lib)
else()
    target_link_libraries(test_synth PRIVATE ft8_lib m)
endif()

# L2 State Machine Test
add_executable(test_l2
    test_l2_state_machine.cpp
    tx_state_machine.cpp
    decode_helper.cpp
)
target_include_directories(test_l2 PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_l2 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l2 PRIVATE ft8_lib m)
endif()

# Golden WAV generator (not a ctest — run manually to regenerate committed WAVs)
# Usage: cd build && ./Release/gen_golden.exe ../golden   (Windows)
#        cd build && ./gen_golden ../golden               (Linux/macOS)
add_executable(gen_golden
    gen_golden.cpp
    decode_helper.cpp
    synth.c
)
target_include_directories(gen_golden PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(gen_golden PRIVATE ft8_lib)
else()
    target_link_libraries(gen_golden PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(gen_golden PRIVATE -Wno-format)
endif()

# L3 TX Poll-Timing test — validates that FT4 TX requires ≤2 ms main-loop poll
add_executable(test_l3
    test_l3_tx_poll_timing.cpp
    decode_helper.cpp
    synth.c
)
target_include_directories(test_l3 PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_l3 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l3 PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(test_l3 PRIVATE -Wno-format)
endif()

# Golden RX test — decodes committed WAV files; validates decoder in isolation
add_executable(test_golden_rx
    test_golden_rx.cpp
    decode_helper.cpp
)
target_include_directories(test_golden_rx PRIVATE ${CMAKE_SOURCE_DIR})
# Inject absolute path to golden/ directory so ctest finds the files regardless
# of working directory.  Forward slashes work on all platforms including MSVC.
target_compile_definitions(test_golden_rx PRIVATE
    "GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/golden\""
)
if(WIN32)
    target_link_libraries(test_golden_rx PRIVATE ft8_lib)
else()
    target_link_libraries(test_golden_rx PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(test_golden_rx PRIVATE -Wno-format)
endif()

# TA command format test — validates qmx_set_tone_hz() string formatting
add_executable(test_ta_format test_ta_format.cpp)
target_include_directories(test_ta_format PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_ta_format PRIVATE ft8_lib)
else()
    target_link_libraries(test_ta_format PRIVATE ft8_lib m)
endif()

# L4 TX Timer-Isolation test — validates ISOLATED (esp_timer) vs COUPLED (main loop)
add_executable(test_l4
    test_l4_tx_timer_isolation.cpp
    decode_helper.cpp
)
target_include_directories(test_l4 PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_l4 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l4 PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(test_l4 PRIVATE -Wno-format)
endif()

# WAV diagnostic tool — run manually, not via ctest
# Usage: test_wav_diag <file.wav> [expected_message] [ft8|ft4]
add_executable(test_wav_diag
    test_wav_diag.cpp
    decode_helper.cpp
)
target_include_directories(test_wav_diag PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(test_wav_diag PRIVATE ft8_lib)
else()
    target_link_libraries(test_wav_diag PRIVATE ft8_lib m)
endif()
if(NOT MSVC)
    target_compile_options(test_wav_diag PRIVATE -Wno-format)
endif()

# Enable testing
enable_testing()
add_test(NAME simple             COMMAND test_simple)
add_test(NAME l1_encoder         COMMAND test_l1)
add_test(NAME l2_state_machine   COMMAND test_l2)
add_test(NAME l3_poll_timing     COMMAND test_l3)
add_test(NAME l4_timer_isolation COMMAND test_l4)
add_test(NAME golden_rx          COMMAND test_golden_rx)
add_test(NAME ta_format          COMMAND test_ta_format)
