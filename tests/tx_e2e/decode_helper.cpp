#include "decode_helper.h"
#include "../../components/ft8_lib/ft8/decode.h"
#include "../../components/ft8_lib/common/monitor.h"
#include <cstring>
#include <cctype>
#include <cstdio>
#include <vector>
#include <map>
#include <string>

// Hash table stub: stores hashes recorded during encode, looks them up during decode
static std::map<uint32_t, std::string> g_hash_table;

static bool hash_lookup(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign) {
    auto it = g_hash_table.find(hash);
    if (it != g_hash_table.end()) {
        strncpy(callsign, it->second.c_str(), 11);
        return true;
    }
    return false;
}

static void hash_save(const char* callsign, uint32_t n22) {
    g_hash_table[n22] = std::string(callsign);
}

static ftx_callsign_hash_interface_t g_hash_if = {
    .lookup_hash = hash_lookup,
    .save_hash = hash_save
};

ftx_callsign_hash_interface_t* decode_get_hash_if(void) {
    return &g_hash_if;
}

void decode_clear_hashes(void) {
    g_hash_table.clear();
}

void normalize_text(const char* text, char* out_norm, int out_len) {
    if (!text || !out_norm || out_len <= 0) return;

    char* dst = out_norm;
    char* end = out_norm + out_len - 1;  // leave room for null terminator
    bool prev_space = true;

    for (const char* src = text; *src && dst < end; src++) {
        char c = *src;

        // Convert to uppercase
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }

        // Skip leading whitespace
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!prev_space && dst > out_norm) {
                *dst++ = ' ';
                prev_space = true;
            }
        } else {
            *dst++ = c;
            prev_space = false;
        }
    }

    // Strip trailing whitespace
    while (dst > out_norm && (*(dst - 1) == ' ' || *(dst - 1) == '\t')) {
        dst--;
    }

    *dst = '\0';
}

DecodeResult decode_pcm(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto) {
    DecodeResult result = {false, "", 0.0f};

    // Use firmware-compatible settings to fit monitor.c's static buffer (MONITOR_NFFT_MAX=960)
    // At 6kHz with freq_osr=1: nfft = 960, matches static allocation exactly
    monitor_config_t cfg = {
        .f_min = 200.0f,
        .f_max = 2900.0f,
        .sample_rate = fs,        // Use input sample rate (test provides 6kHz)
        .time_osr = 2,            // Firmware default
        .freq_osr = 1,            // Critical: >= 2 would exceed MONITOR_NFFT_MAX (960)
        .protocol = proto
    };

    monitor_t mon;
    fprintf(stderr, "calling monitor_init...\n");
    fflush(stderr);
    monitor_init(&mon, &cfg);
    fprintf(stderr, "calling monitor_reset...\n");
    fflush(stderr);
    monitor_reset(&mon);

    // Process audio in blocks
    int block_size = mon.block_size;
    for (int i = 0; i + block_size <= n_samples; i += block_size) {
        monitor_process(&mon, samples + i);
    }

    // Find candidates
    ftx_candidate_t candidates[20];
    int num_candidates = ftx_find_candidates(&mon.wf, 20, candidates, 0);

    // Try to decode each candidate
    for (int i = 0; i < num_candidates; i++) {
        ftx_message_t msg;
        ftx_decode_status_t status;

        if (ftx_decode_candidate(&mon.wf, &candidates[i], 4, &msg, &status)) {
            // Decode succeeded!
            char text[256];
            ftx_message_offsets_t offsets = {};
            ftx_message_decode(&msg, &g_hash_if, text, &offsets);

            // Normalize the decoded text
            normalize_text(text, result.text, sizeof(result.text));
            result.found = true;
            result.snr = candidates[i].score;
            break;  // Return first successful decode
        }
    }

    monitor_free(&mon);
    return result;
}
