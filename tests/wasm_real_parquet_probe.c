#include <carquet/carquet.h>
#include <stdint.h>

/* Returns:
 *   high 32 bits: row count
 *   low 16 bits: column count
 *   bits 16-31: status code on failure
 */
uint64_t carquet_wasm_real_parquet_probe(const uint8_t* data, size_t size) {
    carquet_error_t error = CARQUET_ERROR_INIT;
    carquet_reader_options_t options;
    carquet_reader_options_init(&options);
    options.use_mmap = false;
    options.num_threads = 1;

    carquet_reader_t* reader = carquet_reader_open_buffer(data, size, &options, &error);
    if (!reader) {
        return ((uint64_t)(uint32_t)error.code) << 16;
    }

    int64_t rows = carquet_reader_num_rows(reader);
    int32_t columns = carquet_reader_num_columns(reader);
    carquet_reader_close(reader);

    if (rows < 0 || columns < 0) {
        return ((uint64_t)(uint32_t)CARQUET_ERROR_INVALID_METADATA) << 16;
    }

    return ((uint64_t)(uint32_t)rows << 32) | (uint32_t)columns;
}
