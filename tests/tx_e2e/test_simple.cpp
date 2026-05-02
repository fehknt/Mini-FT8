#include <cstdio>
#include "../../components/ft8_lib/ft8/message.h"
#include "../../components/ft8_lib/ft8/encode.h"
#include "../../components/ft8_lib/ft8/constants.h"

// C-compatible hash stub functions
extern "C" {
    static bool hash_lookup_stub(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign) {
        return false;  // No hashes in our stub
    }

    static void hash_save_stub(const char* callsign, uint32_t n22) {
        // Do nothing - stub implementation
    }
}

int main() {
    printf("Starting simple encode test...\n");
    fflush(stdout);

    ftx_message_t msg;
    ftx_callsign_hash_interface_t hash_if = {
        .lookup_hash = hash_lookup_stub,
        .save_hash = hash_save_stub
    };
    const char* text = "CQ TEST FN42";

    printf("Calling ftx_message_encode with text: %s\n", text);
    fflush(stdout);

    ftx_message_rc_t rc = ftx_message_encode(&msg, &hash_if, text);

    printf("Encode result: %d\n", rc);
    fflush(stdout);

    if (rc == FTX_MESSAGE_RC_OK) {
        printf("Message encoded successfully\n");
        printf("Payload bytes: ");
        for (int i = 0; i < 10; i++) {
            printf("%02x ", msg.payload[i]);
        }
        printf("\n");
        fflush(stdout);

        uint8_t tones[FT8_NN];
        printf("Calling ft8_encode...\n");
        fflush(stdout);
        ft8_encode(msg.payload, tones);
        printf("Encode complete\n");
        fflush(stdout);
    } else {
        printf("Encode failed with code %d\n", rc);
    }

    return 0;
}
