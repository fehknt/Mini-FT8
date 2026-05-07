#pragma once

#include "ft8/constants.h"

struct ProtocolConfig {
    ftx_protocol_t protocol_id;
    const char* name;
    int slot_time_ms;
    float symbol_period;
    int data_symbols;
    int total_symbols;
    float tone_spacing;
};

inline const ProtocolConfig kProtocolFT8 = {
    .protocol_id = FTX_PROTOCOL_FT8,
    .name = "FT8",
    .slot_time_ms = 15000,
    .symbol_period = FT8_SYMBOL_PERIOD, // 0.160f
    .data_symbols = FT8_ND,
    .total_symbols = FT8_NN,
    .tone_spacing = 6.25f
};

inline const ProtocolConfig kProtocolFT4 = {
    .protocol_id = FTX_PROTOCOL_FT4,
    .name = "FT4",
    .slot_time_ms = 7500,
    .symbol_period = FT4_SYMBOL_PERIOD, // 0.048f
    .data_symbols = FT4_ND,
    .total_symbols = FT4_NN,
    .tone_spacing = 20.8333f
};

extern volatile const ProtocolConfig* g_protocol;  // volatile for dual-core cache coherency

// Bumped by the UI thread whenever g_protocol changes. Stream tasks watch
// this value and re-init their monitor_t in place at the next slot boundary
// so we never tear down USB (which would disconnect QMX CAT).
#include <stdint.h>
extern volatile uint32_t g_protocol_change_seq;
