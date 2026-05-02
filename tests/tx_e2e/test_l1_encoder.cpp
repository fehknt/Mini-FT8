#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include "decode_helper.h"
#include "synth.h"
#include "../../components/ft8_lib/ft8/encode.h"

// Test case structure
struct TestCase {
    const char* text;
    ftx_protocol_t proto;
    float base_hz;
};

// L1 Test cases (from the plan)
static const TestCase CASES[] = {
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
    {"W1ABC K9XYZ FN42",    FTX_PROTOCOL_FT4, 1500.0f},
    {"W1ABC K9XYZ -12",     FTX_PROTOCOL_FT4, 1500.0f},
    {"W1ABC K9XYZ R-08",    FTX_PROTOCOL_FT4, 1500.0f},
    {"W1ABC K9XYZ RR73",    FTX_PROTOCOL_FT4, 1500.0f},
    {"W1ABC K9XYZ 73",      FTX_PROTOCOL_FT4, 1500.0f},

    // Edge offsets
    {"CQ TEST FN42",        FTX_PROTOCOL_FT8,  300.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT8, 2700.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT4,  300.0f},
    {"CQ TEST FN42",        FTX_PROTOCOL_FT4, 2700.0f},

    // Free text
    {"HELLO WORLD",         FTX_PROTOCOL_FT8, 1500.0f},
    {"73 GL",               FTX_PROTOCOL_FT4, 1500.0f},
};

static const int NUM_CASES = sizeof(CASES) / sizeof(CASES[0]);

int main() {
    int passed = 0, failed = 0;
    const int SAMPLE_RATE = 12000;
    const float AMPLITUDE = 0.5f;

    printf("L1 Encoder Test — %d cases\n", NUM_CASES);
    printf("======================================\n\n");

    for (int i = 0; i < NUM_CASES; i++) {
        const TestCase& c = CASES[i];
        printf("[%d/%d] %s (%s @ %.0f Hz): ",
               i + 1, NUM_CASES,
               (c.proto == FTX_PROTOCOL_FT8) ? "FT8" : "FT4",
               c.text, c.base_hz);
        fflush(stdout);

        // Step 1: Encode message to payload
        fprintf(stderr, "  encoding message...\n");
        fflush(stderr);
        ftx_message_t msg;
        ftx_callsign_hash_interface_t hash_if = {nullptr, nullptr};
        ftx_message_rc_t rc = ftx_message_encode(&msg, &hash_if, c.text);
        fprintf(stderr, "  encode result: %d\n", rc);
        fflush(stderr);
        if (rc != FTX_MESSAGE_RC_OK) {
            printf("FAIL (encode error %d)\n", rc);
            failed++;
            continue;
        }

        // Step 2: Encode payload to tones
        uint8_t tones[FT8_NN > FT4_NN ? FT8_NN : FT4_NN];
        int nn;
        float sym_period, spacing;
        if (c.proto == FTX_PROTOCOL_FT8) {
            ft8_encode(msg.payload, tones);
            nn = FT8_NN;
            sym_period = FT8_SYMBOL_PERIOD;
            spacing = 6.25f;
        } else {
            ft4_encode(msg.payload, tones);
            nn = FT4_NN;
            sym_period = FT4_SYMBOL_PERIOD;
            spacing = 20.8333f;
        }

        // Step 3: Synthesize audio with pre/post padding
        int padding_samples = (int)(0.5f * SAMPLE_RATE);  // 0.5s padding
        int tone_samples = (int)(nn * sym_period * SAMPLE_RATE);
        int total_samples = padding_samples + tone_samples + padding_samples;

        std::vector<float> pcm(total_samples, 0.0f);

        int synth_len = 0;
        synth_fsk(tones, nn, sym_period, c.base_hz, spacing,
                  SAMPLE_RATE, AMPLITUDE,
                  pcm.data() + padding_samples, &synth_len);

        // Step 4: Decode the audio
        DecodeResult decode_result = decode_pcm(pcm.data(), pcm.size(), SAMPLE_RATE, c.proto);

        // Step 5: Compare results
        char norm_input[256], norm_output[256];
        normalize_text(c.text, norm_input, sizeof(norm_input));
        if (!decode_result.found) {
            printf("FAIL (no decode)\n");
            failed++;
            continue;
        }

        if (strcmp(norm_input, decode_result.text) == 0) {
            printf("PASS (SNR=%.1f)\n", decode_result.snr);
            passed++;
        } else {
            printf("FAIL (expected='%s', got='%s')\n", norm_input, decode_result.text);
            failed++;
        }
    }

    printf("\n======================================\n");
    printf("Results: %d/%d passed\n", passed, passed + failed);
    printf("Exit code: %d\n", failed > 0 ? 1 : 0);
    return (failed > 0) ? 1 : 0;
}
