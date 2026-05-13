/**
 * @file column_reader.c
 * @brief Column reading implementation
 */

#include <carquet/carquet.h>
#include "reader_internal.h"
#include "thrift/parquet_types.h"
#include "encoding/plain.h"
#include "encoding/rle.h"
#include "core/endian.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration */
extern carquet_status_t carquet_read_next_page(
    carquet_column_reader_t* reader,
    void* values,
    int64_t max_values,
    int16_t* def_levels,
    int16_t* rep_levels,
    int64_t* values_read,
    carquet_error_t* error);

/* ============================================================================
 * Batch Reading
 * ============================================================================
 */

static int64_t count_present_levels(
    const int16_t* def_levels,
    int64_t count,
    int16_t max_def_level) {

    int64_t present = 0;
    for (int64_t i = 0; i < count; i++) {
        if (def_levels[i] == max_def_level) {
            present++;
        }
    }
    return present;
}

int64_t carquet_column_read_batch(
    carquet_column_reader_t* reader,
    void* values,
    int64_t max_values,
    int16_t* def_levels,
    int16_t* rep_levels) {

    /* max_values < 0 is invalid; max_values = 0 is a "peek" to trigger page loading */
    if (max_values < 0) {
        return -1;
    }
    if (max_values == 0) {
        /* Load page if needed, but don't read any values */
        if (reader->values_remaining > 0 && !reader->page_loaded) {
            carquet_error_t error = CARQUET_ERROR_INIT;
            carquet_status_t status = carquet_column_ensure_page_loaded(reader, &error);
            (void)status;
        }
        return 0;
    }

    if (reader->values_remaining <= 0) {
        return 0;
    }

    carquet_error_t error = CARQUET_ERROR_INIT;
    int64_t total_read = 0;
    int64_t dense_values_read = 0;
    size_t value_size = 0;
    int16_t* scratch_def_levels = NULL;

    /* Determine value size for pointer arithmetic */
    switch (reader->type) {
        case CARQUET_PHYSICAL_BOOLEAN:
            value_size = 1;
            break;
        case CARQUET_PHYSICAL_INT32:
        case CARQUET_PHYSICAL_FLOAT:
            value_size = 4;
            break;
        case CARQUET_PHYSICAL_INT64:
        case CARQUET_PHYSICAL_DOUBLE:
            value_size = 8;
            break;
        case CARQUET_PHYSICAL_INT96:
            value_size = 12;
            break;
        case CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY:
            value_size = reader->type_length;
            break;
        case CARQUET_PHYSICAL_BYTE_ARRAY:
            /* Variable length - handled differently */
            value_size = sizeof(carquet_byte_array_t);
            break;
        default:
            return -1;
    }

    if (reader->max_def_level > 0 && !def_levels) {
        scratch_def_levels = malloc((size_t)max_values * sizeof(*scratch_def_levels));
        if (!scratch_def_levels) {
            return -1;
        }
    }

    /* Read pages until we have enough values or run out */
    while (total_read < max_values && reader->values_remaining > 0) {
        int64_t values_read = 0;
        int64_t to_read = max_values - total_read;
        bool nullable = reader->max_def_level > 0;

        uint8_t* value_ptr = (uint8_t*)values +
            (size_t)(nullable ? dense_values_read : total_read) * value_size;
        int16_t* def_ptr = def_levels ? def_levels + total_read :
            (scratch_def_levels ? scratch_def_levels + total_read : NULL);
        int16_t* rep_ptr = rep_levels ? rep_levels + total_read : NULL;

        carquet_status_t status = carquet_read_next_page(
            reader, value_ptr, to_read, def_ptr, rep_ptr, &values_read, &error);

        if (status != CARQUET_OK) {
            if (total_read > 0) {
                /* Return what we have so far */
                break;
            }
            free(scratch_def_levels);
            return -1;
        }

        if (values_read == 0) {
            break;
        }

        if (nullable && def_ptr) {
            dense_values_read += count_present_levels(
                def_ptr, values_read, reader->max_def_level);
        } else {
            dense_values_read += values_read;
        }
        total_read += values_read;
    }

    free(scratch_def_levels);
    return total_read;
}

/* ============================================================================
 * Skip Values
 * ============================================================================
 */

int64_t carquet_column_skip(
    carquet_column_reader_t* reader,
    int64_t num_values) {

    /* reader is nonnull per API contract */
    if (num_values <= 0 || reader->values_remaining <= 0) {
        return 0;
    }

    /* Allocate temporary buffer for skipping */
    size_t value_size = 0;
    switch (reader->type) {
        case CARQUET_PHYSICAL_BOOLEAN:
            value_size = 1;
            break;
        case CARQUET_PHYSICAL_INT32:
        case CARQUET_PHYSICAL_FLOAT:
            value_size = 4;
            break;
        case CARQUET_PHYSICAL_INT64:
        case CARQUET_PHYSICAL_DOUBLE:
            value_size = 8;
            break;
        case CARQUET_PHYSICAL_INT96:
            value_size = 12;
            break;
        case CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY:
            value_size = reader->type_length;
            break;
        case CARQUET_PHYSICAL_BYTE_ARRAY:
            value_size = sizeof(carquet_byte_array_t);
            break;
        default:
            return 0;
    }

    /* Read and discard values in chunks */
    int64_t total_skipped = 0;
    int64_t chunk_size = 1024;

    void* temp = malloc(chunk_size * value_size);
    if (!temp) {
        return 0;
    }

    while (total_skipped < num_values && reader->values_remaining > 0) {
        int64_t to_skip = num_values - total_skipped;
        if (to_skip > chunk_size) {
            to_skip = chunk_size;
        }

        int64_t skipped = carquet_column_read_batch(reader, temp, to_skip, NULL, NULL);
        if (skipped <= 0) {
            break;
        }
        total_skipped += skipped;
    }

    free(temp);
    return total_skipped;
}
