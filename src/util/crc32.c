/**
 * @file crc32.c
 * @brief CRC32 checksum for Parquet page checksums
 *
 * Parquet page CRCs use the standard IEEE CRC32 polynomial (0x04C11DB7),
 * computed over the serialized page body and excluding the page header.
 *
 * Delegates to zlib's crc32 when compression support is enabled. Builds that
 * intentionally omit compression use a compact table-based fallback to keep
 * the checksum API available without pulling zlib into the link.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef CARQUET_NO_COMPRESSION
#include <zlib.h>
#endif

uint32_t carquet_crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
#ifndef CARQUET_NO_COMPRESSION
    /* zlib's crc32 takes uInt (32-bit) lengths; chunk if needed. */
    uLong c = (uLong)crc;
    while (length > 0) {
        uInt chunk = length > (size_t)0xFFFFFFF0u ? (uInt)0xFFFFFFF0u : (uInt)length;
        c = crc32(c, (const Bytef*)data, chunk);
        data += chunk;
        length -= chunk;
    }
    return (uint32_t)c;
#else
    uint32_t c = crc ^ 0xffffffffu;

    for (size_t i = 0; i < length; i++) {
        c ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            c = (c >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(c & 1u));
        }
    }

    return c ^ 0xffffffffu;
#endif
}

uint32_t carquet_crc32(const uint8_t* data, size_t length) {
    return carquet_crc32_update(0, data, length);
}
