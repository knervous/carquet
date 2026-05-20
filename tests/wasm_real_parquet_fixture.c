#include <carquet/carquet.h>
#include <stdio.h>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "carquet-wasm-real.parquet";
    carquet_error_t error = CARQUET_ERROR_INIT;
    carquet_status_t status;

    carquet_schema_t* schema = carquet_schema_create(&error);
    if (!schema) {
        fprintf(stderr, "schema: %s\n", error.message);
        return 1;
    }

    status = carquet_schema_add_column(
        schema, "id", CARQUET_PHYSICAL_INT64, NULL,
        CARQUET_REPETITION_REQUIRED, 0, 0);
    if (status != CARQUET_OK) {
        fprintf(stderr, "id column: %s\n", carquet_status_string(status));
        carquet_schema_free(schema);
        return 1;
    }

    status = carquet_schema_add_column(
        schema, "value", CARQUET_PHYSICAL_DOUBLE, NULL,
        CARQUET_REPETITION_REQUIRED, 0, 0);
    if (status != CARQUET_OK) {
        fprintf(stderr, "value column: %s\n", carquet_status_string(status));
        carquet_schema_free(schema);
        return 1;
    }

    carquet_writer_options_t options;
    carquet_writer_options_init(&options);
    options.compression = CARQUET_COMPRESSION_UNCOMPRESSED;
    options.write_crc = false;

    const int64_t ids[] = {101, 102, 103};
    const double values[] = {1.25, 2.5, 5.0};
    const carquet_column_view_t columns[] = {
        {0, ids, 3, NULL, NULL},
        {1, values, 3, NULL, NULL},
    };

    status = carquet_save_file_with_options(
        path, schema, &options, columns, 2, &error);
    carquet_schema_free(schema);

    if (status != CARQUET_OK) {
        fprintf(stderr, "save: %s %s\n", carquet_status_string(status), error.message);
        return 1;
    }

    return 0;
}
