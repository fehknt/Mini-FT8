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

# Enable testing
enable_testing()
add_test(NAME simple COMMAND test_simple)
add_test(NAME l1_encoder COMMAND test_l1)

# L2 State Machine Test (placeholder for now)
# add_executable(test_l2
#     test_l2_state_machine.cpp
#     tx_state_machine.cpp
#     cat_recorder.cpp
#     decode_helper.cpp
#     synth.c
# )
# target_include_directories(test_l2 PRIVATE ${CMAKE_SOURCE_DIR})
# target_link_libraries(test_l2 PRIVATE ft8_lib m)
# add_test(NAME l2_state_machine COMMAND test_l2)
