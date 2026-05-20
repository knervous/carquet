#include <stdint.h>

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>

void carquet_wasm128_bitunpack8_4bit(const uint8_t* input, uint32_t* values) {
    uint8_t bytes[16] = {
        input[0], input[1], input[2], input[3],
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const v128_t packed = wasm_v128_load(bytes);
    const v128_t mask = wasm_i8x16_splat(0x0f);
    const v128_t low = wasm_v128_and(packed, mask);
    const v128_t high = wasm_v128_and(wasm_u8x16_shr(packed, 4), mask);
    const v128_t nibbles = wasm_i8x16_shuffle(
        low, high,
        0, 16, 1, 17, 2, 18, 3, 19,
        8, 9, 10, 11, 12, 13, 14, 15);
    const v128_t words = wasm_u16x8_extend_low_u8x16(nibbles);
    const v128_t lo = wasm_u32x4_extend_low_u16x8(words);
    const v128_t hi = wasm_u32x4_extend_high_u16x8(words);

    wasm_v128_store(values, lo);
    wasm_v128_store(values + 4, hi);
}

void carquet_wasm128_bitunpack8_8bit(const uint8_t* input, uint32_t* values) {
    uint8_t bytes[16] = {
        input[0], input[1], input[2], input[3],
        input[4], input[5], input[6], input[7],
        0, 0, 0, 0, 0, 0, 0, 0
    };
    const v128_t packed = wasm_v128_load(bytes);
    const v128_t words = wasm_u16x8_extend_low_u8x16(packed);
    const v128_t lo = wasm_u32x4_extend_low_u16x8(words);
    const v128_t hi = wasm_u32x4_extend_high_u16x8(words);

    wasm_v128_store(values, lo);
    wasm_v128_store(values + 4, hi);
}
#endif
