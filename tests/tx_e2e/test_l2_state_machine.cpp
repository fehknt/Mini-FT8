#include <cstdio>
#include <cstring>
#include "tx_state_machine.h"
#include "decode_helper.h"

int main() {
    printf("L2 State Machine Test\n");
    printf("======================================\n\n");

    int passed = 0, failed = 0;

    // Test case 1: Simple CQ on FT8
    {
        printf("[1/2] FT8 CQ TEST FN42 (skip_tones=0): ");
        fflush(stdout);

        TxConfig cfg = {
            .protocol_name = "FT8",
            .protocol = FTX_PROTOCOL_FT8,
            .base_hz = 1500.0f,
            .slot_start_ms = 0,
            .skip_tones = 0,
            .text = "W1ABC K9XYZ FN42",
        };

        decode_clear_hashes();
        auto events = run_tx(cfg, 2);
        auto pcm = events_to_pcm(events, 6000, 0.5f);

        DecodeResult res = decode_pcm(pcm.data(), pcm.size(), 6000, FTX_PROTOCOL_FT8);

        if (res.found) {
            printf("PASS (SNR=%.1f)\n", res.snr);
            passed++;
        } else {
            printf("FAIL (no decode)\n");
            failed++;
        }
    }

    // Test case 2: FT4 with skip_tones=5
    {
        printf("[2/2] FT4 W1ABC K9XYZ (skip_tones=5): ");
        fflush(stdout);

        TxConfig cfg = {
            .protocol_name = "FT4",
            .protocol = FTX_PROTOCOL_FT4,
            .base_hz = 1500.0f,
            .slot_start_ms = 0,
            .skip_tones = 5,
            .text = "W1ABC K9XYZ -12",
        };

        decode_clear_hashes();
        auto events = run_tx(cfg, 2);
        auto pcm = events_to_pcm(events, 6000, 0.5f);

        DecodeResult res = decode_pcm(pcm.data(), pcm.size(), 6000, FTX_PROTOCOL_FT4);

        if (res.found) {
            printf("PASS (SNR=%.1f)\n", res.snr);
            passed++;
        } else {
            printf("FAIL (no decode)\n");
            failed++;
        }
    }

    printf("\n======================================\n");
    printf("Results: %d/%d passed\n", passed, passed + failed);
    return (failed > 0) ? 1 : 0;
}
