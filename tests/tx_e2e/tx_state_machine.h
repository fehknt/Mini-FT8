#ifndef _TX_E2E_STATE_MACHINE_H_
#define _TX_E2E_STATE_MACHINE_H_

#include <stdint.h>
#include <vector>
#include "../../components/ft8_lib/ft8/constants.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration for TX state machine
struct TxConfig {
    const char* protocol_name;   // "FT8" or "FT4"
    ftx_protocol_t protocol;
    float base_hz;
    int64_t slot_start_ms;       // Simulated wall clock at slot boundary
    int skip_tones;              // How many tones to skip before TX
    const char* text;            // Message to transmit
};

#ifdef __cplusplus
}

// CAT command event
struct CatEvent {
    int64_t time_ms;
    enum Kind { TX_BEGIN, TA, TX_END } kind;
    float tone_hz;  // Valid for TA
};

// Run TX state machine, return CAT event stream
std::vector<CatEvent> run_tx(const TxConfig& cfg, int64_t loop_delay_ms = 2);

// Synthesize PCM from CAT event stream
std::vector<float> events_to_pcm(const std::vector<CatEvent>& events,
                                 int sample_rate, float amplitude);

#endif

#endif // _TX_E2E_STATE_MACHINE_H_
