#include <cstdio>
#include <vector>
#include "../../components/ft8_lib/ft8/message.h"
#include "../../components/ft8_lib/ft8/encode.h"
#include "../../components/ft8_lib/ft8/constants.h"
#include "synth.h"

extern "C" {
    static bool hash_lookup_stub(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign) {
        return false;
    }

    static void hash_save_stub(const char* callsign, uint32_t n22) {
    }
}

int main() {
    printf("Testing synth...\n");
    fflush(stdout);

    // Encode a message
    ftx_message_t msg;
    ftx_callsign_hash_interface_t hash_if = {
        .lookup_hash = hash_lookup_stub,
        .save_hash = hash_save_stub
    };
    ftx_message_encode(&msg, &hash_if, "CQ TEST FN42");

    // Encode to tones
    uint8_t tones[FT8_NN];
    ft8_encode(msg.payload, tones);

    printf("Generated %d tones\n", FT8_NN);
    fflush(stdout);

    // Synthesize
    const int SAMPLE_RATE = 12000;
    const float AMPLITUDE = 0.5f;
    const float SYM_PERIOD = FT8_SYMBOL_PERIOD;
    const float SPACING = 6.25f;
    const float BASE_HZ = 1500.0f;

    int padding_samples = (int)(0.5f * SAMPLE_RATE);
    int tone_samples = (int)(FT8_NN * SYM_PERIOD * SAMPLE_RATE);
    int total_samples = padding_samples + tone_samples + padding_samples;

    std::vector<float> pcm(total_samples, 0.0f);

    printf("Calling synth_fsk with %d samples...\n", tone_samples);
    fflush(stdout);

    int synth_len = 0;
    synth_fsk(tones, FT8_NN, SYM_PERIOD, BASE_HZ, SPACING,
              SAMPLE_RATE, AMPLITUDE,
              pcm.data() + padding_samples, &synth_len);

    printf("Synth complete: %d samples generated\n", synth_len);
    printf("Total PCM: %zu samples\n", pcm.size());
    fflush(stdout);

    return 0;
}
