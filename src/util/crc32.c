/**
 * @file crc32.c
 * @brief CRC32C (Castagnoli) checksum as required by the Parquet specification
 *
 * Uses hardware CRC32C instructions on x86 (SSE4.2) and ARM when available,
 * falling back to slicing-by-8 software implementation.
 *
 * Note: Parquet uses CRC32C (polynomial 0x1EDC6F41, reflected as 0x82F63B78),
 * NOT the IEEE CRC32 (polynomial 0x04C11DB7, reflected as 0xEDB88320).
 * The SSE4.2 _mm_crc32_* instructions compute CRC32C natively.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Forward declaration: SIMD-dispatched CRC32C (initialized by carquet_init) */
extern uint32_t carquet_dispatch_crc32c(uint32_t crc, const uint8_t* data, size_t len);

/* ============================================================================
 * Slicing-by-8 CRC32C Software Fallback (Castagnoli polynomial 0x82F63B78)
 *
 * Processes 8 bytes per iteration using 8 precomputed lookup tables.
 * This is the scalar fallback when hardware CRC32C is not available.
 * ============================================================================
 */

#define CRC32C_POLY 0x82F63B78u

/* 8 tables of 256 entries for slicing-by-8 */
static uint32_t crc32c_tables[8][256];
static volatile int crc32c_tables_initialized = 0;

static void crc32c_init_tables(void) {
    if (crc32c_tables_initialized) return;

    /* Generate base table (reflected CRC32C / Castagnoli) */
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? ((crc >> 1) ^ CRC32C_POLY) : (crc >> 1);
        }
        crc32c_tables[0][i] = crc;
    }

    /* Generate extended tables for slicing-by-8 */
    for (int k = 1; k < 8; k++) {
        for (int i = 0; i < 256; i++) {
            crc32c_tables[k][i] = (crc32c_tables[k - 1][i] >> 8) ^
                                   crc32c_tables[0][crc32c_tables[k - 1][i] & 0xFF];
        }
    }

    crc32c_tables_initialized = 1;
}

/* Software CRC32C used as the scalar fallback in the SIMD dispatch table */
uint32_t carquet_scalar_crc32c(uint32_t crc, const uint8_t* data, size_t length) {
    if (!crc32c_tables_initialized) crc32c_init_tables();

    crc = ~crc;

    /* Process 8 bytes at a time */
    while (length >= 8) {
        uint32_t one, two;
        memcpy(&one, data, 4);
        memcpy(&two, data + 4, 4);
        one ^= crc;

        crc = crc32c_tables[7][ one        & 0xFF] ^
              crc32c_tables[6][(one >>  8) & 0xFF] ^
              crc32c_tables[5][(one >> 16) & 0xFF] ^
              crc32c_tables[4][(one >> 24)       ] ^
              crc32c_tables[3][ two        & 0xFF] ^
              crc32c_tables[2][(two >>  8) & 0xFF] ^
              crc32c_tables[1][(two >> 16) & 0xFF] ^
              crc32c_tables[0][(two >> 24)       ];

        data += 8;
        length -= 8;
    }

    /* Process remaining bytes */
    while (length--) {
        crc = crc32c_tables[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

/* ============================================================================
 * Public API — routes through SIMD dispatch for hardware CRC32C
 * ============================================================================
 */

uint32_t carquet_crc32(const uint8_t* data, size_t length) {
    return carquet_dispatch_crc32c(0, data, length);
}

uint32_t carquet_crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
    return carquet_dispatch_crc32c(crc, data, length);
}
