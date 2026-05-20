/**
 * @file buffer_reader.c
 * @brief In-memory buffer reader entry point for non-mmap builds.
 */

#include "core/allocator.h"
#include <carquet/carquet.h>
#include "reader_internal.h"
#include "../core/endian.h"
#include <stdlib.h>
#include <string.h>

carquet_reader_t* carquet_reader_open_buffer(
    const void* buffer,
    size_t size,
    const carquet_reader_options_t* options,
    carquet_error_t* error) {

    if (size == 0) {
        CARQUET_SET_ERROR(error, CARQUET_ERROR_INVALID_ARGUMENT, "Invalid buffer size");
        return NULL;
    }

    carquet_reader_t* reader = carquet_mem_calloc(1, sizeof(carquet_reader_t));
    if (!reader) {
        CARQUET_SET_ERROR(error, CARQUET_ERROR_OUT_OF_MEMORY, "Failed to allocate reader");
        return NULL;
    }

    reader->mmap_data = (const uint8_t*)buffer;
    reader->file_size = size;
    reader->owns_file = false;

    if (options) {
        reader->options = *options;
    } else {
        carquet_reader_options_init(&reader->options);
    }

    if (carquet_arena_init(&reader->arena) != CARQUET_OK) {
        carquet_mem_free(reader);
        CARQUET_SET_ERROR(error, CARQUET_ERROR_OUT_OF_MEMORY, "Failed to initialize arena");
        return NULL;
    }

    if (size < 12) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        CARQUET_SET_ERROR(error, CARQUET_ERROR_INVALID_FOOTER, "Buffer too small");
        return NULL;
    }

    if (memcmp(buffer, "PAR1", 4) != 0) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        CARQUET_SET_ERROR(error, CARQUET_ERROR_INVALID_MAGIC, "Invalid header magic");
        return NULL;
    }

    const uint8_t* end = (const uint8_t*)buffer + size;
    if (memcmp(end - 4, "PAR1", 4) != 0) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        CARQUET_SET_ERROR(error, CARQUET_ERROR_INVALID_MAGIC, "Invalid footer magic");
        return NULL;
    }

    uint32_t footer_size = carquet_read_u32_le(end - 8);
    if (footer_size > size - 8) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        CARQUET_SET_ERROR(error, CARQUET_ERROR_INVALID_FOOTER, "Footer size too large");
        return NULL;
    }

    const uint8_t* footer_data = end - 8 - footer_size;
    carquet_status_t status = parquet_parse_file_metadata(
        footer_data, footer_size, &reader->arena, &reader->metadata, error);
    if (status != CARQUET_OK) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        return NULL;
    }

    reader->schema = build_schema(&reader->arena, &reader->metadata, error);
    if (!reader->schema) {
        carquet_arena_destroy(&reader->arena);
        carquet_mem_free(reader);
        return NULL;
    }

    reader->is_open = true;
    return reader;
}
