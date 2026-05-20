#include <stdint.h>

void carquet_wasm128_bitunpack8_4bit(const uint8_t* input, uint32_t* values);

int carquet_wasm128_external_link_probe(void) {
    const uint8_t packed[] = {0x10, 0x32, 0x54, 0x76};
    uint32_t values[8] = {0};

    carquet_wasm128_bitunpack8_4bit(packed, values);

    for (uint32_t i = 0; i < 8; i++) {
        if (values[i] != i) {
            return 0;
        }
    }

    return 1;
}
