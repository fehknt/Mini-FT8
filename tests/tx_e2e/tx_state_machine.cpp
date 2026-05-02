#include "tx_state_machine.h"
#include "../../components/ft8_lib/ft8/message.h"
#include "../../components/ft8_lib/ft8/encode.h"
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
    static bool stub_hash_lookup(ftx_callsign_hash_type_t, uint32_t, char*) { return false; }
    static void stub_hash_save(const char*, uint32_t) {}
}

std::vector<CatEvent> run_tx(const TxConfig& cfg, int64_t loop_delay_ms) {
    std::vector<CatEvent> events;
    int64_t now = cfg.slot_start_ms;

    // tx_start: encode message
    ftx_message_t msg;
    ftx_callsign_hash_interface_t hash_if = {stub_hash_lookup, stub_hash_save};
    ftx_message_encode(&msg, &hash_if, cfg.text);

    // Encode to tones
    uint8_t tones[FT8_NN];
    int nn, sym_ms;
    float spacing;

    if (cfg.protocol == FTX_PROTOCOL_FT8) {
        ft8_encode(msg.payload, tones);
        nn = FT8_NN;
        sym_ms = (int)(FT8_SYMBOL_PERIOD * 1000.0f);
        spacing = 6.25f;
    } else {
        ft4_encode(msg.payload, tones);
        nn = FT4_NN;
        sym_ms = (int)(FT4_SYMBOL_PERIOD * 1000.0f);
        spacing = 20.8333f;
    }

    // Initial setup
    int tone_idx = cfg.skip_tones;
    if (tone_idx >= nn) tone_idx = nn;

    int64_t next_tone_time = cfg.slot_start_ms + (int64_t)tone_idx * sym_ms;

    // TX_BEGIN event
    events.push_back({now, CatEvent::TX_BEGIN, 0.0f});

    // First TA from tx_start
    if (tone_idx < nn) {
        float hz = cfg.base_hz + spacing * tones[tone_idx];
        events.push_back({now, CatEvent::TA, hz});
    }

    int last_ta_int = -1, last_ta_frac = -1;
    auto record_ta = [&](float hz) {
        int ta_int = (int)lrintf(hz);
        int ta_frac = (int)lrintf((hz - ta_int) * 100.0f);
        if (ta_int != last_ta_int || ta_frac != last_ta_frac) {
            last_ta_int = ta_int;
            last_ta_frac = ta_frac;
            events.push_back({now, CatEvent::TA, hz});
        }
    };

    // tx_tick loop
    while (tone_idx < nn) {
        if (now < next_tone_time) {
            now += loop_delay_ms;
            continue;
        }
        float hz = cfg.base_hz + spacing * tones[tone_idx];
        record_ta(hz);
        tone_idx++;
        next_tone_time = cfg.slot_start_ms + (int64_t)tone_idx * sym_ms;
    }

    // TX_END event
    events.push_back({now, CatEvent::TX_END, 0.0f});

    return events;
}

std::vector<float> events_to_pcm(const std::vector<CatEvent>& events,
                                 int sample_rate, float amplitude) {
    if (events.empty()) return {};

    int64_t t_start = events.front().time_ms;
    int64_t t_end = events.back().time_ms;
    int64_t total_ms = t_end - t_start + 500;  // tail silence
    int total_samples = (int)((double)total_ms * sample_rate / 1000.0);

    std::vector<float> pcm(total_samples, 0.0f);

    double phase = 0.0;
    float current_hz = 0.0f;
    bool tx_active = false;

    size_t evt_idx = 0;
    for (int i = 0; i < total_samples; i++) {
        int64_t t_ms = t_start + (int64_t)((double)i * 1000.0 / sample_rate);

        // Advance events
        while (evt_idx < events.size() && events[evt_idx].time_ms <= t_ms) {
            switch (events[evt_idx].kind) {
                case CatEvent::TX_BEGIN: tx_active = true; break;
                case CatEvent::TA: current_hz = events[evt_idx].tone_hz; break;
                case CatEvent::TX_END: tx_active = false; break;
            }
            evt_idx++;
        }

        // Generate sample
        if (tx_active && current_hz > 0) {
            pcm[i] = amplitude * sinf((float)phase);
            phase += 2.0 * M_PI * current_hz / sample_rate;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
    }

    return pcm;
}
