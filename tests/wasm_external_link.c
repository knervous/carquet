#include <carquet/carquet.h>
#include <stdint.h>
#include <string.h>

extern int carquet_gzip_compress(
    const uint8_t* src,
    size_t src_size,
    uint8_t* dst,
    size_t dst_capacity,
    size_t* dst_size,
    int level);
extern int carquet_gzip_decompress(
    const uint8_t* src,
    size_t src_size,
    uint8_t* dst,
    size_t dst_capacity,
    size_t* dst_size);
extern size_t carquet_gzip_compress_bound(size_t src_size);

int carquet_wasm_external_link_probe(void) {
    int major = -1;
    int minor = -1;
    int patch = -1;
    const char* version = carquet_version();

    carquet_version_components(&major, &minor, &patch);
    return version[0] == '0' &&
           major == CARQUET_VERSION_MAJOR &&
           minor == CARQUET_VERSION_MINOR &&
           patch == CARQUET_VERSION_PATCH;
}

int carquet_wasm_gzip_external_link_probe(void) {
    const uint8_t source[] = "carquet gzip wasm probe";
    uint8_t compressed[128] = {0};
    uint8_t restored[sizeof(source)] = {0};
    size_t compressed_size = 0;
    size_t restored_size = 0;

    if (carquet_gzip_compress_bound(sizeof(source)) > sizeof(compressed)) {
        return 0;
    }

    if (carquet_gzip_compress(source, sizeof(source),
                              compressed, sizeof(compressed),
                              &compressed_size, 6) != CARQUET_OK) {
        return 0;
    }

    if (carquet_gzip_decompress(compressed, compressed_size,
                                restored, sizeof(restored),
                                &restored_size) != CARQUET_OK) {
        return 0;
    }

    return restored_size == sizeof(source) &&
           memcmp(restored, source, sizeof(source)) == 0;
}
