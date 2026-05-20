/**
 * @file helpers.c
 * @brief Host-facing helper wrappers for common Carquet workflows.
 */

#include <carquet/carquet.h>
#include <carquet/error.h>
#include <stdlib.h>
#include <string.h>

static carquet_status_t helper_error(
    carquet_error_t* error,
    carquet_status_t status,
    const char* message) {
    carquet_error_set(error, status, __FILE__, __LINE__, __func__, "%s", message);
    return status;
}

static void helper_release_partial_block(carquet_data_block_t* block) {
    if (!block || !block->columns) return;

    for (int32_t column_index = 0; column_index < block->num_columns; column_index++) {
        carquet_block_column_t* column = &block->columns[column_index];
        if (column->type == CARQUET_PHYSICAL_BYTE_ARRAY && column->values) {
            carquet_byte_array_t* values = (carquet_byte_array_t*)column->values;
            for (int64_t row = 0; row < column->num_values; row++) {
                free(values[row].data);
            }
        }
        free(column->values);
        free(column->null_bitmap);
    }
    free(block->columns);
    memset(block, 0, sizeof(*block));
}

#ifdef CARQUET_NO_FILE_IO

carquet_status_t carquet_open_file_with_options(
    const char* path,
    const carquet_reader_options_t* options,
    carquet_reader_t** reader,
    carquet_error_t* error) {
    (void)path;
    (void)options;
    if (reader) *reader = NULL;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "file reader helpers are not available in this build");
}

carquet_status_t carquet_save_file_with_options(
    const char* path,
    const carquet_schema_t* schema,
    const carquet_writer_options_t* options,
    const carquet_column_view_t* columns,
    int32_t num_columns,
    carquet_error_t* error) {
    (void)path;
    (void)schema;
    (void)options;
    (void)columns;
    (void)num_columns;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "file writer helpers are not available in this build");
}

carquet_status_t carquet_save_buffer_with_options(
    const carquet_schema_t* schema,
    const carquet_writer_options_t* options,
    const carquet_column_view_t* columns,
    int32_t num_columns,
    void** buffer,
    size_t* size,
    carquet_error_t* error) {
#ifdef CARQUET_NO_WRITER
    (void)schema;
    (void)options;
    (void)columns;
    (void)num_columns;
    if (buffer) *buffer = NULL;
    if (size) *size = 0;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "writer helpers are not available in this build");
#else
    if (!schema || !buffer || !size || (num_columns > 0 && !columns) || num_columns < 0) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "schema, output buffer, output size, and valid column views are required");
    }

    *buffer = NULL;
    *size = 0;

    carquet_writer_t* writer = carquet_writer_create_buffer(schema, options, error);
    if (!writer) {
        return error ? error->code : CARQUET_ERROR_FILE_OPEN;
    }

    for (int32_t i = 0; i < num_columns; i++) {
        carquet_status_t status = carquet_writer_write_batch(
            writer,
            columns[i].column_index,
            columns[i].values,
            columns[i].num_values,
            columns[i].def_levels,
            columns[i].rep_levels);
        if (status != CARQUET_OK) {
            carquet_writer_abort(writer);
            return status;
        }
    }

    carquet_status_t status = carquet_writer_close(writer);
    if (status != CARQUET_OK) {
        return status;
    }

    return carquet_writer_get_buffer(writer, buffer, size);
#endif
}

carquet_status_t carquet_save_buffer_with_compression(
    const carquet_schema_t* schema,
    carquet_compression_t compression,
    int32_t compression_level,
    const carquet_column_view_t* columns,
    int32_t num_columns,
    void** buffer,
    size_t* size,
    carquet_error_t* error) {
#ifdef CARQUET_NO_WRITER
    (void)schema;
    (void)compression;
    (void)compression_level;
    (void)columns;
    (void)num_columns;
    if (buffer) *buffer = NULL;
    if (size) *size = 0;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "writer helpers are not available in this build");
#else
    carquet_writer_options_t options;
    carquet_writer_options_init(&options);
    options.compression = compression;
    options.compression_level = compression_level;
    return carquet_save_buffer_with_options(
        schema, &options, columns, num_columns, buffer, size, error);
#endif
}

carquet_status_t carquet_update_cell_in_file(
    const char* input_path,
    const char* output_path,
    const carquet_cell_location_t* location,
    const carquet_cell_value_t* value,
    const carquet_cell_update_options_t* options,
    carquet_error_t* error) {
    (void)input_path;
    (void)output_path;
    (void)location;
    (void)value;
    (void)options;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "cell update helpers are not available in this build");
}

carquet_status_t carquet_retrieve_block(
    carquet_reader_t* reader,
    const carquet_block_range_t* range,
    const carquet_block_options_t* options,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    (void)reader;
    (void)range;
    (void)options;
    if (block) memset(block, 0, sizeof(*block));
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "block retrieval helpers are not available in this build");
}

carquet_status_t carquet_retrieve_block_from_file(
    const char* path,
    const carquet_block_range_t* range,
    const carquet_block_options_t* options,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    (void)path;
    (void)range;
    (void)options;
    if (block) memset(block, 0, sizeof(*block));
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "block retrieval helpers are not available in this build");
}

#else

static size_t helper_type_size(carquet_physical_type_t type) {
    switch (type) {
        case CARQUET_PHYSICAL_BOOLEAN:
            return sizeof(uint8_t);
        case CARQUET_PHYSICAL_INT32:
            return sizeof(int32_t);
        case CARQUET_PHYSICAL_INT64:
            return sizeof(int64_t);
        case CARQUET_PHYSICAL_INT96:
            return sizeof(carquet_int96_t);
        case CARQUET_PHYSICAL_FLOAT:
            return sizeof(float);
        case CARQUET_PHYSICAL_DOUBLE:
            return sizeof(double);
        case CARQUET_PHYSICAL_BYTE_ARRAY:
            return sizeof(carquet_byte_array_t);
        default:
            return 0;
    }
}

static bool helper_bitmap_is_set(const uint8_t* bitmap, int64_t index) {
    return !bitmap || (bitmap[index / 8] & (uint8_t)(1u << (index % 8)));
}

static void helper_bitmap_set(uint8_t* bitmap, int64_t index, bool value) {
    uint8_t mask = (uint8_t)(1u << (index % 8));
    if (value) {
        bitmap[index / 8] |= mask;
    } else {
        bitmap[index / 8] &= (uint8_t)~mask;
    }
}

carquet_status_t carquet_open_file_with_options(
    const char* path,
    const carquet_reader_options_t* options,
    carquet_reader_t** reader,
    carquet_error_t* error) {
    if (!path || !reader) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "path and reader output are required");
    }

    *reader = carquet_reader_open(path, options, error);
    return *reader ? CARQUET_OK : (error ? error->code : CARQUET_ERROR_FILE_OPEN);
}

carquet_status_t carquet_save_file_with_options(
    const char* path,
    const carquet_schema_t* schema,
    const carquet_writer_options_t* options,
    const carquet_column_view_t* columns,
    int32_t num_columns,
    carquet_error_t* error) {
#ifdef CARQUET_NO_WRITER
    (void)path;
    (void)schema;
    (void)options;
    (void)columns;
    (void)num_columns;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "writer helpers are not available in this build");
#else
    if (!path || !schema || (num_columns > 0 && !columns) || num_columns < 0) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "path, schema, and valid column views are required");
    }

    carquet_writer_t* writer = carquet_writer_create(path, schema, options, error);
    if (!writer) {
        return error ? error->code : CARQUET_ERROR_FILE_OPEN;
    }

    for (int32_t i = 0; i < num_columns; i++) {
        carquet_status_t status = carquet_writer_write_batch(
            writer,
            columns[i].column_index,
            columns[i].values,
            columns[i].num_values,
            columns[i].def_levels,
            columns[i].rep_levels);
        if (status != CARQUET_OK) {
            carquet_writer_abort(writer);
            return status;
        }
    }

    return carquet_writer_close(writer);
#endif
}

carquet_status_t carquet_save_buffer_with_options(
    const carquet_schema_t* schema,
    const carquet_writer_options_t* options,
    const carquet_column_view_t* columns,
    int32_t num_columns,
    void** buffer,
    size_t* size,
    carquet_error_t* error) {
#ifdef CARQUET_NO_WRITER
    (void)schema;
    (void)options;
    (void)columns;
    (void)num_columns;
    if (buffer) *buffer = NULL;
    if (size) *size = 0;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "buffer writer helpers are not available in this build");
#else
    if (!schema || !buffer || !size || (num_columns > 0 && !columns) || num_columns < 0) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "schema, output buffer, output size, and valid column views are required");
    }

    *buffer = NULL;
    *size = 0;

    carquet_writer_t* writer = carquet_writer_create_buffer(schema, options, error);
    if (!writer) {
        return error ? error->code : CARQUET_ERROR_FILE_OPEN;
    }

    for (int32_t i = 0; i < num_columns; i++) {
        carquet_status_t status = carquet_writer_write_batch(
            writer,
            columns[i].column_index,
            columns[i].values,
            columns[i].num_values,
            columns[i].def_levels,
            columns[i].rep_levels);
        if (status != CARQUET_OK) {
            carquet_writer_abort(writer);
            return status;
        }
    }

    carquet_status_t status = carquet_writer_close(writer);
    if (status != CARQUET_OK) {
        return status;
    }

    return carquet_writer_get_buffer(writer, buffer, size);
#endif
}

carquet_status_t carquet_update_cell_in_file(
    const char* input_path,
    const char* output_path,
    const carquet_cell_location_t* location,
    const carquet_cell_value_t* value,
    const carquet_cell_update_options_t* options,
    carquet_error_t* error) {
    (void)input_path;
    (void)output_path;
    (void)location;
    (void)value;
    (void)options;
    return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                        "cell updates require a full-file rewrite helper that is not implemented yet");
}

static carquet_status_t helper_init_block(
    const carquet_schema_t* schema,
    const carquet_block_range_t* range,
    const carquet_block_options_t* options,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    int64_t requested_rows = range->end_row - range->start_row;
    int32_t requested_columns = range->end_column - range->start_column;

    memset(block, 0, sizeof(*block));
    block->start_row = range->start_row;
    block->num_columns = requested_columns;
    block->columns = (carquet_block_column_t*)calloc((size_t)requested_columns, sizeof(*block->columns));
    if (!block->columns) {
        return helper_error(error, CARQUET_ERROR_OUT_OF_MEMORY,
                            "failed to allocate block columns");
    }

    for (int32_t i = 0; i < requested_columns; i++) {
        int32_t source_column = range->start_column + i;
        carquet_physical_type_t type = carquet_schema_column_type(schema, source_column);
        size_t type_size = helper_type_size(type);
        if (type_size == 0) {
            helper_release_partial_block(block);
            return helper_error(error, CARQUET_ERROR_NOT_IMPLEMENTED,
                                "block retrieval does not support this physical type yet");
        }

        carquet_block_column_t* column = &block->columns[i];
        column->column_index = source_column;
        column->type = type;
        column->values_size = (size_t)requested_rows * type_size;
        column->values = calloc((size_t)requested_rows, type_size);
        if (!column->values) {
            helper_release_partial_block(block);
            return helper_error(error, CARQUET_ERROR_OUT_OF_MEMORY,
                                "failed to allocate block values");
        }

        if (!options || options->include_null_bitmap) {
            column->null_bitmap_size = ((size_t)requested_rows + 7u) / 8u;
            column->null_bitmap = (uint8_t*)calloc(column->null_bitmap_size, 1);
            if (!column->null_bitmap) {
                helper_release_partial_block(block);
                return helper_error(error, CARQUET_ERROR_OUT_OF_MEMORY,
                                    "failed to allocate block null bitmap");
            }
        }
    }

    return CARQUET_OK;
}

static carquet_status_t helper_copy_block_rows(
    const carquet_row_batch_t* batch,
    int64_t batch_row_offset,
    int64_t rows_to_copy,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    for (int32_t column_index = 0; column_index < block->num_columns; column_index++) {
        const void* data = NULL;
        const uint8_t* null_bitmap = NULL;
        int64_t num_values = 0;
        carquet_status_t status = carquet_row_batch_column(
            batch, column_index, &data, &null_bitmap, &num_values);
        if (status != CARQUET_OK) return status;
        if (batch_row_offset + rows_to_copy > num_values) {
            return helper_error(error, CARQUET_ERROR_INVALID_METADATA,
                                "batch column is shorter than expected");
        }

        carquet_block_column_t* column = &block->columns[column_index];
        int64_t dest_offset = column->num_values;

        if (column->type == CARQUET_PHYSICAL_BYTE_ARRAY) {
            const carquet_byte_array_t* src = (const carquet_byte_array_t*)data;
            carquet_byte_array_t* dst = (carquet_byte_array_t*)column->values;
            for (int64_t row = 0; row < rows_to_copy; row++) {
                int64_t src_row = batch_row_offset + row;
                int64_t dst_row = dest_offset + row;
                bool is_set = helper_bitmap_is_set(null_bitmap, src_row);
                if (column->null_bitmap) helper_bitmap_set(column->null_bitmap, dst_row, is_set);
                if (is_set && src[src_row].length > 0) {
                    dst[dst_row].data = (uint8_t*)malloc((size_t)src[src_row].length);
                    if (!dst[dst_row].data) {
                        return helper_error(error, CARQUET_ERROR_OUT_OF_MEMORY,
                                            "failed to copy byte-array cell");
                    }
                    memcpy(dst[dst_row].data, src[src_row].data, (size_t)src[src_row].length);
                    dst[dst_row].length = src[src_row].length;
                }
            }
        } else {
            size_t type_size = helper_type_size(column->type);
            const uint8_t* src = (const uint8_t*)data + (size_t)batch_row_offset * type_size;
            uint8_t* dst = (uint8_t*)column->values + (size_t)dest_offset * type_size;
            memcpy(dst, src, (size_t)rows_to_copy * type_size);
            for (int64_t row = 0; column->null_bitmap && row < rows_to_copy; row++) {
                bool is_set = helper_bitmap_is_set(null_bitmap, batch_row_offset + row);
                helper_bitmap_set(column->null_bitmap, dest_offset + row, is_set);
            }
        }

        column->num_values += rows_to_copy;
    }

    block->num_rows += rows_to_copy;
    return CARQUET_OK;
}

carquet_status_t carquet_retrieve_block(
    carquet_reader_t* reader,
    const carquet_block_range_t* range,
    const carquet_block_options_t* options,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    if (!reader || !range || !block) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "reader, range, and block output are required");
    }
    if (range->start_row < 0 || range->end_row < range->start_row ||
        range->start_column < 0 || range->end_column <= range->start_column) {
        return helper_error(error, CARQUET_ERROR_INVALID_ARGUMENT,
                            "invalid block range");
    }
    if (range->end_column > carquet_reader_num_columns(reader)) {
        return helper_error(error, CARQUET_ERROR_COLUMN_NOT_FOUND,
                            "block range exceeds available columns");
    }

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    carquet_status_t status = helper_init_block(schema, range, options, block, error);
    if (status != CARQUET_OK) return status;

    int32_t num_projected = range->end_column - range->start_column;
    int32_t* projected = (int32_t*)malloc((size_t)num_projected * sizeof(*projected));
    if (!projected) {
        helper_release_partial_block(block);
        return helper_error(error, CARQUET_ERROR_OUT_OF_MEMORY,
                            "failed to allocate projection");
    }
    for (int32_t i = 0; i < num_projected; i++) {
        projected[i] = range->start_column + i;
    }

    carquet_batch_reader_config_t config;
    carquet_batch_reader_config_init(&config);
    config.batch_size = options && options->batch_size > 0 ? options->batch_size : 65536;
    config.column_indices = projected;
    config.num_columns = num_projected;

    carquet_batch_reader_t* batch_reader = carquet_batch_reader_create(reader, &config, error);
    free(projected);
    if (!batch_reader) {
        helper_release_partial_block(block);
        return error ? error->code : CARQUET_ERROR_INTERNAL;
    }

    int64_t absolute_row = 0;
    carquet_row_batch_t* batch = NULL;
    while ((status = carquet_batch_reader_next(batch_reader, &batch)) == CARQUET_OK && batch) {
        int64_t batch_rows = carquet_row_batch_num_rows(batch);
        int64_t batch_start = absolute_row;
        int64_t batch_end = absolute_row + batch_rows;

        if (batch_end > range->start_row && batch_start < range->end_row) {
            int64_t copy_start = range->start_row > batch_start ? range->start_row - batch_start : 0;
            int64_t copy_end = range->end_row < batch_end ? range->end_row - batch_start : batch_rows;
            status = helper_copy_block_rows(batch, copy_start, copy_end - copy_start, block, error);
            if (status != CARQUET_OK) {
                carquet_row_batch_free(batch);
                carquet_batch_reader_free(batch_reader);
                helper_release_partial_block(block);
                return status;
            }
        }

        absolute_row = batch_end;
        carquet_row_batch_free(batch);
        batch = NULL;
        if (absolute_row >= range->end_row) break;
    }

    carquet_batch_reader_free(batch_reader);
    if (status != CARQUET_OK && status != CARQUET_ERROR_END_OF_DATA) {
        helper_release_partial_block(block);
        return status;
    }

    return CARQUET_OK;
}

carquet_status_t carquet_retrieve_block_from_file(
    const char* path,
    const carquet_block_range_t* range,
    const carquet_block_options_t* options,
    carquet_data_block_t* block,
    carquet_error_t* error) {
    carquet_reader_t* reader = NULL;
    carquet_status_t status = carquet_open_file_with_options(path, NULL, &reader, error);
    if (status != CARQUET_OK) return status;

    status = carquet_retrieve_block(reader, range, options, block, error);
    carquet_reader_close(reader);
    return status;
}

#endif

void carquet_data_block_free(carquet_data_block_t* block) {
    helper_release_partial_block(block);
}
