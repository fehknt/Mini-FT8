#ifndef _TX_E2E_DECODE_HELPER_H_
#define _TX_E2E_DECODE_HELPER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../../components/ft8_lib/ft8/constants.h"
#include "../../components/ft8_lib/ft8/message.h"

typedef struct {
    bool found;
    char text[256];
    float snr;
} DecodeResult;

/// Decode PCM samples using ft8_lib reference decoder
///
/// @param samples     PCM samples (float, -1.0 to 1.0 range)
/// @param n_samples   Number of samples
/// @param fs          Sample rate in Hertz
/// @param proto       Protocol: FTX_PROTOCOL_FT8 or FTX_PROTOCOL_FT4
/// @return DecodeResult with .found=true if successful
DecodeResult decode_pcm(const float* samples, int n_samples, int fs,
                        ftx_protocol_t proto);

/// Normalize message text for comparison
/// - Convert to uppercase
/// - Collapse whitespace
/// - Strip leading/trailing whitespace
/// - Handle <HASH> callsign notation
///
/// @param text        Input text
/// @param out_norm    Output buffer for normalized text (pre-allocated)
/// @param out_len     Size of output buffer
void normalize_text(const char* text, char* out_norm, int out_len);

#ifdef __cplusplus
}
#endif

#endif // _TX_E2E_DECODE_HELPER_H_
