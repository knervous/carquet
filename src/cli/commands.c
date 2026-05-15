/**
 * @file commands.c
 * @brief Implementation of carquet CLI commands
 */

#include "cli.h"
#include "core/compat.h"
#include "reader/reader_internal.h"
#include "thrift/parquet_types.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

/* Shared layout constants (defined early so commands above the table
 * helpers below can reference them). */
#define MAX_COL_WIDTH 40
#define MAX_VALUE_BUF 256

/* Forward declaration: implementation lives further down with the other
 * tabular-output helpers. */
static void print_dyn_table(const char* const* headers, int32_t num_cols,
                             const char* const* cells, int64_t num_rows);

/* ══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════════ */

const char* cli_repetition_name(carquet_field_repetition_t rep) {
    switch (rep) {
        case CARQUET_REPETITION_REQUIRED: return "REQUIRED";
        case CARQUET_REPETITION_OPTIONAL: return "OPTIONAL";
        case CARQUET_REPETITION_REPEATED: return "REPEATED";
        default: return "?";
    }
}

void cli_format_type(carquet_physical_type_t phys,
                     const carquet_logical_type_t* logical,
                     char* buf, size_t buf_size)
{
    const char* base = carquet_physical_type_name(phys);
    if (!logical || logical->id == CARQUET_LOGICAL_UNKNOWN) {
        snprintf(buf, buf_size, "%s", base);
        return;
    }
    switch (logical->id) {
        case CARQUET_LOGICAL_STRING:    snprintf(buf, buf_size, "STRING"); break;
        case CARQUET_LOGICAL_DATE:      snprintf(buf, buf_size, "DATE"); break;
        case CARQUET_LOGICAL_UUID:      snprintf(buf, buf_size, "UUID"); break;
        case CARQUET_LOGICAL_JSON:      snprintf(buf, buf_size, "JSON"); break;
        case CARQUET_LOGICAL_ENUM:      snprintf(buf, buf_size, "ENUM"); break;
        case CARQUET_LOGICAL_LIST:      snprintf(buf, buf_size, "LIST"); break;
        case CARQUET_LOGICAL_MAP:       snprintf(buf, buf_size, "MAP"); break;
        case CARQUET_LOGICAL_FLOAT16:   snprintf(buf, buf_size, "FLOAT16"); break;
        case CARQUET_LOGICAL_VARIANT:   snprintf(buf, buf_size, "VARIANT"); break;
        case CARQUET_LOGICAL_GEOMETRY:  snprintf(buf, buf_size, "GEOMETRY"); break;
        case CARQUET_LOGICAL_GEOGRAPHY: snprintf(buf, buf_size, "GEOGRAPHY"); break;
        case CARQUET_LOGICAL_NULL:      snprintf(buf, buf_size, "NULL"); break;
        case CARQUET_LOGICAL_BSON:      snprintf(buf, buf_size, "BSON"); break;
        case CARQUET_LOGICAL_DECIMAL:
            snprintf(buf, buf_size, "DECIMAL(%d,%d)",
                     logical->params.decimal.precision,
                     logical->params.decimal.scale);
            break;
        case CARQUET_LOGICAL_INTEGER:
            snprintf(buf, buf_size, "%sINT%d",
                     logical->params.integer.is_signed ? "" : "U",
                     logical->params.integer.bit_width);
            break;
        case CARQUET_LOGICAL_TIME: {
            const char* unit = "?";
            switch (logical->params.time.unit) {
                case CARQUET_TIME_UNIT_MILLIS: unit = "ms"; break;
                case CARQUET_TIME_UNIT_MICROS: unit = "us"; break;
                case CARQUET_TIME_UNIT_NANOS:  unit = "ns"; break;
            }
            snprintf(buf, buf_size, "TIME(%s%s)", unit,
                     logical->params.time.is_adjusted_to_utc ? ",UTC" : "");
            break;
        }
        case CARQUET_LOGICAL_TIMESTAMP: {
            const char* unit = "?";
            switch (logical->params.timestamp.unit) {
                case CARQUET_TIME_UNIT_MILLIS: unit = "ms"; break;
                case CARQUET_TIME_UNIT_MICROS: unit = "us"; break;
                case CARQUET_TIME_UNIT_NANOS:  unit = "ns"; break;
            }
            snprintf(buf, buf_size, "TIMESTAMP(%s%s)", unit,
                     logical->params.timestamp.is_adjusted_to_utc ? ",UTC" : "");
            break;
        }
        default:
            snprintf(buf, buf_size, "%s", base);
            break;
    }
}

void cli_format_bytes(int64_t bytes, char* buf, size_t buf_size) {
    if (bytes < 1024)
        snprintf(buf, buf_size, "%" PRId64 " B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, buf_size, "%.1f MB", bytes / (1024.0 * 1024));
    else
        snprintf(buf, buf_size, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
}

const char* cli_format_value(carquet_physical_type_t type,
                             const void* value, int32_t type_len,
                             const carquet_logical_type_t* logical,
                             char* buf, size_t buf_size)
{
    if (!value) { snprintf(buf, buf_size, "null"); return buf; }

    /* Handle logical type formatting */
    if (logical && logical->id == CARQUET_LOGICAL_DATE && type == CARQUET_PHYSICAL_INT32) {
        int32_t days = *(const int32_t*)value;
        time_t t = (time_t)days * 86400;
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        snprintf(buf, buf_size, "%04d-%02d-%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return buf;
    }

    if (logical && logical->id == CARQUET_LOGICAL_TIMESTAMP) {
        int64_t val = *(const int64_t*)value;
        time_t secs;
        int frac = 0;
        const char* frac_fmt = "";
        switch (logical->params.timestamp.unit) {
            case CARQUET_TIME_UNIT_MILLIS:
                secs = (time_t)(val / 1000);
                frac = (int)(val % 1000);
                frac_fmt = ".%03d";
                break;
            case CARQUET_TIME_UNIT_MICROS:
                secs = (time_t)(val / 1000000);
                frac = (int)(val % 1000000);
                frac_fmt = ".%06d";
                break;
            case CARQUET_TIME_UNIT_NANOS:
                secs = (time_t)(val / 1000000000LL);
                frac = (int)(val % 1000000000LL);
                frac_fmt = ".%09d";
                break;
        }
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &secs);
#else
        gmtime_r(&secs, &tm);
#endif
        int n = snprintf(buf, buf_size, "%04d-%02d-%02dT%02d:%02d:%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
        if (frac != 0 && n > 0 && (size_t)n < buf_size)
            snprintf(buf + n, buf_size - (size_t)n, frac_fmt, frac);
        return buf;
    }

    switch (type) {
        case CARQUET_PHYSICAL_BOOLEAN:
            snprintf(buf, buf_size, "%s", *(const uint8_t*)value ? "true" : "false");
            break;
        case CARQUET_PHYSICAL_INT32:
            snprintf(buf, buf_size, "%" PRId32, *(const int32_t*)value);
            break;
        case CARQUET_PHYSICAL_INT64:
            snprintf(buf, buf_size, "%" PRId64, *(const int64_t*)value);
            break;
        case CARQUET_PHYSICAL_FLOAT:
            snprintf(buf, buf_size, "%g", (double)*(const float*)value);
            break;
        case CARQUET_PHYSICAL_DOUBLE:
            snprintf(buf, buf_size, "%g", *(const double*)value);
            break;
        case CARQUET_PHYSICAL_BYTE_ARRAY: {
            const carquet_byte_array_t* ba = (const carquet_byte_array_t*)value;
            /* Check if it looks like a string (logical STRING or UTF8) */
            bool is_string = logical && (logical->id == CARQUET_LOGICAL_STRING ||
                                          logical->id == CARQUET_LOGICAL_JSON ||
                                          logical->id == CARQUET_LOGICAL_ENUM);
            if (is_string || 1) {
                /* Try to print as string, truncate if long */
                int32_t len = ba->length;
                int32_t max_len = (int32_t)(buf_size - 1);
                if (len > max_len) len = max_len;
                memcpy(buf, ba->data, (size_t)len);
                buf[len] = '\0';
            }
            break;
        }
        case CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY: {
            /* Print as hex */
            const uint8_t* bytes = (const uint8_t*)value;
            int32_t len = type_len;
            if (len > (int32_t)(buf_size / 2 - 1)) len = (int32_t)(buf_size / 2 - 1);
            for (int32_t i = 0; i < len; i++)
                snprintf(buf + i * 2, buf_size - (size_t)(i * 2), "%02x", bytes[i]);
            break;
        }
        case CARQUET_PHYSICAL_INT96: {
            const uint32_t* v96 = (const uint32_t*)value;
            snprintf(buf, buf_size, "0x%08x%08x%08x", v96[2], v96[1], v96[0]);
            break;
        }
        default:
            snprintf(buf, buf_size, "?");
            break;
    }
    return buf;
}

static carquet_reader_t* open_or_die(const char* path, carquet_error_t* err) {
    carquet_reader_t* reader = carquet_reader_open(path, NULL, err);
    if (!reader) {
        fprintf(stderr, "error: %s\n", err->message);
    }
    return reader;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_schema
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_schema(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t n = carquet_schema_num_elements(schema);

    printf("message schema {\n");

    /*
     * Parquet schema is stored as a flat list with num_children to define tree
     * structure (Thrift-style DFS pre-order). We track depth with a stack of
     * remaining children counts.
     */
    int depth_stack[64] = {0};
    int depth = 0;
    /* Element 0 is root "schema", its children count tells us how many
     * top-level elements follow */
    const carquet_schema_node_t* root = carquet_schema_get_element(schema, 0);
    /* Get num_children from internal struct */
    const parquet_schema_element_t* root_elem = (const parquet_schema_element_t*)root;
    depth_stack[0] = root_elem->num_children;
    depth = 1;

    for (int32_t i = 1; i < n; i++) {
        const carquet_schema_node_t* node = carquet_schema_get_element(schema, i);
        const parquet_schema_element_t* elem = (const parquet_schema_element_t*)node;

        /* Indent */
        for (int d = 0; d < depth; d++) printf("  ");

        if (carquet_schema_node_is_leaf(node)) {
            char type_buf[64];
            cli_format_type(carquet_schema_node_physical_type(node),
                           carquet_schema_node_logical_type(node),
                           type_buf, sizeof(type_buf));
            printf("%s %s %s",
                   cli_repetition_name(carquet_schema_node_repetition(node)),
                   type_buf,
                   carquet_schema_node_name(node));

            int32_t tl = carquet_schema_node_type_length(node);
            if (tl > 0) printf(" (length=%d)", tl);
            printf(";\n");
        } else {
            /* Group node */
            const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
            const char* annotation = "";
            if (lt) {
                switch (lt->id) {
                    case CARQUET_LOGICAL_LIST: annotation = " (LIST)"; break;
                    case CARQUET_LOGICAL_MAP:  annotation = " (MAP)"; break;
                    case CARQUET_LOGICAL_VARIANT: annotation = " (VARIANT)"; break;
                    case CARQUET_LOGICAL_GEOMETRY: annotation = " (GEOMETRY)"; break;
                    case CARQUET_LOGICAL_GEOGRAPHY: annotation = " (GEOGRAPHY)"; break;
                    default: break;
                }
            }
            printf("%s group %s%s {\n",
                   cli_repetition_name(carquet_schema_node_repetition(node)),
                   carquet_schema_node_name(node),
                   annotation);

            /* Push children count */
            if (depth < 63) {
                depth++;
                depth_stack[depth - 1] = elem->num_children;
            }
            continue; /* Don't decrement children count yet */
        }

        /* Decrement parent's children count and close groups */
        depth_stack[depth - 1]--;
        while (depth > 1 && depth_stack[depth - 1] == 0) {
            depth--;
            for (int d = 0; d < depth; d++) printf("  ");
            printf("}\n");
            if (depth > 0) depth_stack[depth - 1]--;
        }
    }

    printf("}\n");
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_info
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_info(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int64_t total_rows = carquet_reader_num_rows(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int32_t num_rgs = carquet_reader_num_row_groups(reader);

    /* Access internal metadata for created_by and key-value metadata */
    const parquet_file_metadata_t* meta = &reader->metadata;

    printf("File:         %s\n", path);
    if (meta && meta->created_by)
        printf("Created by:   %s\n", meta->created_by);
    printf("Rows:         %" PRId64 "\n", total_rows);
    printf("Columns:      %d\n", num_cols);
    printf("Row groups:   %d\n", num_rgs);

    /* Key-value metadata */
    if (meta && meta->num_key_value > 0) {
        printf("\nKey-value metadata:\n");
        for (int32_t i = 0; i < meta->num_key_value; i++) {
            const char* val = meta->key_value_metadata[i].value;
            if (val && strlen(val) > 60) {
                printf("  %-20s %.57s...\n", meta->key_value_metadata[i].key, val);
            } else {
                printf("  %-20s %s\n", meta->key_value_metadata[i].key,
                       val ? val : "(null)");
            }
        }
    }

    /* Column details */
    printf("\nColumns:\n");
    printf("  %-4s %-30s %-20s %-10s\n", "#", "Name", "Type", "Nullable");
    printf("  %-4s %-30s %-20s %-10s\n", "---", "---", "---", "---");
    for (int32_t c = 0; c < num_cols; c++) {
        char type_buf[64];
        cli_format_type(carquet_schema_column_type(schema, c),
                       carquet_schema_node_logical_type(
                           carquet_schema_get_element(schema,
                               schema->leaf_indices[c])),
                       type_buf, sizeof(type_buf));

        const carquet_schema_node_t* node = carquet_schema_get_element(schema,
            schema->leaf_indices[c]);
        bool nullable = carquet_schema_node_repetition(node) != CARQUET_REPETITION_REQUIRED;

        char idx[8];
        snprintf(idx, sizeof(idx), "%d", c);
        printf("  %-4s %-30s %-20s %-10s\n", idx,
               carquet_schema_column_name(schema, c),
               type_buf, nullable ? "yes" : "no");
    }

    /* Row group details */
    printf("\nRow groups:\n");
    printf("  %-4s %-15s %-15s %-15s %-10s\n",
           "#", "Rows", "Uncompressed", "Compressed", "Ratio");
    printf("  %-4s %-15s %-15s %-15s %-10s\n",
           "---", "---", "---", "---", "---");
    for (int32_t rg = 0; rg < num_rgs; rg++) {
        carquet_row_group_metadata_t rgm;
        if (carquet_reader_row_group_metadata(reader, rg, &rgm) != CARQUET_OK)
            continue;
        char uncomp[32], comp[32], ratio[16], idx[8];
        cli_format_bytes(rgm.total_byte_size, uncomp, sizeof(uncomp));
        cli_format_bytes(rgm.total_compressed_size, comp, sizeof(comp));
        if (rgm.total_byte_size > 0)
            snprintf(ratio, sizeof(ratio), "%.1fx",
                     (double)rgm.total_byte_size / (double)rgm.total_compressed_size);
        else
            snprintf(ratio, sizeof(ratio), "-");
        snprintf(idx, sizeof(idx), "%d", rg);
        printf("  %-4s %-15" PRId64 " %-15s %-15s %-10s\n",
               idx, rgm.num_rows, uncomp, comp, ratio);
    }

    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_count
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_count(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    printf("%" PRId64 "\n", carquet_reader_num_rows(reader));
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_columns
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_columns(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    for (int32_t c = 0; c < num_cols; c++) {
        printf("%s\n", carquet_schema_column_name(schema, c));
    }
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_stat
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_stat(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int32_t num_rgs = carquet_reader_num_row_groups(reader);

    static const char* const HEADERS[] = {"Column", "Type", "Nulls", "Min", "Max"};
    const int32_t NCOLS = 5;

    char** cells = calloc((size_t)num_cols * NCOLS, sizeof(char*));
    if (!cells) {
        carquet_reader_close(reader);
        return 1;
    }

    for (int32_t rg = 0; rg < num_rgs; rg++) {
        if (num_rgs > 1)
            printf("Row group %d:\n", rg);

        for (int32_t c = 0; c < num_cols; c++) {
            carquet_column_statistics_t stats;
            carquet_physical_type_t phys = carquet_schema_column_type(schema, c);
            const carquet_schema_node_t* node = carquet_schema_get_element(schema,
                schema->leaf_indices[c]);
            const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
            int32_t tl = carquet_schema_node_type_length(node);

            char type_buf[64];
            cli_format_type(phys, lt, type_buf, sizeof(type_buf));

            char nulls[32] = "-";
            char min_buf[MAX_VALUE_BUF] = "-";
            char max_buf[MAX_VALUE_BUF] = "-";

            if (carquet_reader_column_statistics(reader, rg, c, &stats) == CARQUET_OK) {
                if (stats.has_null_count)
                    snprintf(nulls, sizeof(nulls), "%" PRId64, stats.null_count);
                if (stats.has_min_max) {
                    /* stats.min_value / max_value are raw bytes for BYTE_ARRAY.
                     * cli_format_value expects a carquet_byte_array_t* for that
                     * physical type, so wrap the raw bytes here. */
                    if (phys == CARQUET_PHYSICAL_BYTE_ARRAY) {
                        carquet_byte_array_t min_ba = {
                            .data = (uint8_t*)(uintptr_t)stats.min_value,
                            .length = stats.min_value_size
                        };
                        carquet_byte_array_t max_ba = {
                            .data = (uint8_t*)(uintptr_t)stats.max_value,
                            .length = stats.max_value_size
                        };
                        cli_format_value(phys, &min_ba, tl, lt,
                                         min_buf, sizeof(min_buf));
                        cli_format_value(phys, &max_ba, tl, lt,
                                         max_buf, sizeof(max_buf));
                    } else {
                        cli_format_value(phys, stats.min_value, tl, lt,
                                         min_buf, sizeof(min_buf));
                        cli_format_value(phys, stats.max_value, tl, lt,
                                         max_buf, sizeof(max_buf));
                    }
                }
            }

            cells[c * NCOLS + 0] = carquet_heap_strdup(carquet_schema_column_name(schema, c));
            cells[c * NCOLS + 1] = carquet_heap_strdup(type_buf);
            cells[c * NCOLS + 2] = carquet_heap_strdup(nulls);
            cells[c * NCOLS + 3] = carquet_heap_strdup(min_buf);
            cells[c * NCOLS + 4] = carquet_heap_strdup(max_buf);
        }

        print_dyn_table(HEADERS, NCOLS, (const char* const*)cells, num_cols);

        /* Free this row group's cells before reusing the buffer. */
        for (int32_t i = 0; i < num_cols * NCOLS; i++) {
            free(cells[i]);
            cells[i] = NULL;
        }
        if (rg < num_rgs - 1) printf("\n");
    }

    free(cells);
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_validate
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_validate(const char* path) {
    carquet_error_t err = CARQUET_ERROR_INIT;

    /* Open with checksum verification enabled */
    carquet_reader_options_t opts;
    carquet_reader_options_init(&opts);
    opts.verify_checksums = true;

    carquet_reader_t* reader = carquet_reader_open(path, &opts, &err);
    if (!reader) {
        fprintf(stderr, "INVALID: %s\n", err.message);
        return 1;
    }

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int32_t num_rgs = carquet_reader_num_row_groups(reader);
    int64_t total_rows = carquet_reader_num_rows(reader);
    int errors = 0;

    /* Try to read every column in every row group */
    for (int32_t rg = 0; rg < num_rgs; rg++) {
        for (int32_t c = 0; c < num_cols; c++) {
            carquet_column_reader_t* col = carquet_reader_get_column(reader, rg, c, &err);
            if (!col) {
                fprintf(stderr, "  ERROR: rg=%d col=%d (%s): %s\n",
                        rg, c, carquet_schema_column_name(schema, c), err.message);
                errors++;
                continue;
            }

            /* Read through all pages to trigger CRC checks */
            carquet_physical_type_t phys = carquet_schema_column_type(schema, c);
            int32_t elem_size = carquet_physical_type_size(phys);

            if (elem_size > 0) {
                /* Fixed-size type */
                uint8_t buf[8192];
                int64_t batch = (int64_t)(sizeof(buf) / (size_t)elem_size);
                while (carquet_column_read_batch(col, buf, batch, NULL, NULL) > 0)
                    ;
            } else {
                /* Variable-length type */
                carquet_byte_array_t buf[256];
                while (carquet_column_read_batch(col, buf, 256, NULL, NULL) > 0)
                    ;
            }

            carquet_column_reader_free(col);
        }
    }

    if (errors == 0) {
        printf("OK: %" PRId64 " rows, %d columns, %d row groups - all pages valid\n",
               total_rows, num_cols, num_rgs);
    } else {
        printf("ERRORS: %d page read failures\n", errors);
    }

    carquet_reader_close(reader);
    return errors > 0 ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Table display helpers for head/tail/sample
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char** cells;       /* [row * num_cols + col] */
    int*   widths;      /* per column */
    int32_t num_cols;
    int64_t num_rows;
    int64_t capacity;
    const carquet_schema_t* schema;
} table_t;

static void table_init(table_t* t, const carquet_schema_t* schema, int32_t num_cols, int64_t cap) {
    t->schema = schema;
    t->num_cols = num_cols;
    t->num_rows = 0;
    t->capacity = cap;
    t->cells = calloc((size_t)(cap * num_cols), sizeof(char*));
    t->widths = calloc((size_t)num_cols, sizeof(int));

    /* Initialize widths from column names */
    for (int32_t c = 0; c < num_cols; c++) {
        const char* name = carquet_schema_column_name(schema, c);
        int len = (int)strlen(name);
        t->widths[c] = len < MAX_COL_WIDTH ? len : MAX_COL_WIDTH;
    }
}

static void table_add_cell(table_t* t, int64_t row, int32_t col, const char* value) {
    if (row >= t->capacity || col >= t->num_cols) return;
    t->cells[row * t->num_cols + col] = carquet_heap_strdup(value);
    int len = (int)strlen(value);
    if (len > MAX_COL_WIDTH) len = MAX_COL_WIDTH;
    if (len > t->widths[col]) t->widths[col] = len;
    if (row >= t->num_rows) t->num_rows = row + 1;
}

static void table_print(const table_t* t) {
    /* Header */
    printf("  ");
    for (int32_t c = 0; c < t->num_cols; c++) {
        if (c > 0) printf("  ");
        printf("%-*.*s", t->widths[c], t->widths[c],
               carquet_schema_column_name(t->schema, c));
    }
    printf("\n  ");
    for (int32_t c = 0; c < t->num_cols; c++) {
        if (c > 0) printf("  ");
        for (int w = 0; w < t->widths[c]; w++) putchar('-');
    }
    printf("\n");

    /* Rows */
    for (int64_t r = 0; r < t->num_rows; r++) {
        printf("  ");
        for (int32_t c = 0; c < t->num_cols; c++) {
            if (c > 0) printf("  ");
            const char* val = t->cells[r * t->num_cols + c];
            if (!val) val = "";
            printf("%-*.*s", t->widths[c], t->widths[c], val);
        }
        printf("\n");
    }
}

static void table_free(table_t* t) {
    if (t->cells) {
        for (int64_t i = 0; i < t->capacity * t->num_cols; i++)
            free(t->cells[i]);
        free(t->cells);
    }
    free(t->widths);
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_head
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_head(const char* path, int64_t n) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int64_t total = carquet_reader_num_rows(reader);
    if (n > total) n = total;
    if (n <= 0 || num_cols <= 0) {
        carquet_reader_close(reader);
        return 0;
    }

    table_t tbl;
    table_init(&tbl, schema, num_cols, n);

    /* Read n rows from first row group(s) */
    for (int32_t c = 0; c < num_cols; c++) {
        carquet_physical_type_t phys = carquet_schema_column_type(schema, c);
        const carquet_schema_node_t* node = carquet_schema_get_element(schema,
            schema->leaf_indices[c]);
        const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
        int32_t tl = carquet_schema_node_type_length(node);
        bool nullable = carquet_schema_node_repetition(node) != CARQUET_REPETITION_REQUIRED;
        int16_t max_def = carquet_schema_node_max_def_level(node);

        int64_t rows_read = 0;
        for (int32_t rg = 0; rg < carquet_reader_num_row_groups(reader) && rows_read < n; rg++) {
            carquet_column_reader_t* col = carquet_reader_get_column(reader, rg, c, &err);
            if (!col) continue;

            int64_t want = n - rows_read;

            /* Allocate buffer based on type */
            int32_t elem_size = carquet_physical_type_size(phys);
            void* buf;
            int16_t* def = NULL;
            if (phys == CARQUET_PHYSICAL_BYTE_ARRAY) {
                buf = calloc((size_t)want, sizeof(carquet_byte_array_t));
            } else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY) {
                buf = calloc((size_t)want, (size_t)tl);
            } else {
                buf = calloc((size_t)want, (size_t)elem_size);
            }
            if (nullable)
                def = calloc((size_t)want, sizeof(int16_t));

            int64_t got = carquet_column_read_batch(col, buf, want, def, NULL);

            for (int64_t i = 0; i < got && rows_read + i < n; i++) {
                char vbuf[MAX_VALUE_BUF];
                if (nullable && def && def[i] < max_def) {
                    table_add_cell(&tbl, rows_read + i, c, "null");
                } else {
                    const void* vp = NULL;
                    if (phys == CARQUET_PHYSICAL_BYTE_ARRAY)
                        vp = &((carquet_byte_array_t*)buf)[i];
                    else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY)
                        vp = (uint8_t*)buf + i * tl;
                    else
                        vp = (uint8_t*)buf + i * elem_size;

                    cli_format_value(phys, vp, tl, lt, vbuf, sizeof(vbuf));
                    table_add_cell(&tbl, rows_read + i, c, vbuf);
                }
            }

            rows_read += got;
            free(buf);
            free(def);
            carquet_column_reader_free(col);
        }
    }

    table_print(&tbl);
    table_free(&tbl);
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_tail
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_tail(const char* path, int64_t n) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int32_t num_rgs = carquet_reader_num_row_groups(reader);
    int64_t total = carquet_reader_num_rows(reader);
    if (n > total) n = total;
    if (n <= 0 || num_cols <= 0) {
        carquet_reader_close(reader);
        return 0;
    }

    /* Figure out where to start reading:
     * skip_rows = total - n
     * Find the row group containing the start offset */
    int64_t skip_rows = total - n;

    table_t tbl;
    table_init(&tbl, schema, num_cols, n);

    for (int32_t c = 0; c < num_cols; c++) {
        carquet_physical_type_t phys = carquet_schema_column_type(schema, c);
        const carquet_schema_node_t* node = carquet_schema_get_element(schema,
            schema->leaf_indices[c]);
        const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
        int32_t tl = carquet_schema_node_type_length(node);
        bool nullable = carquet_schema_node_repetition(node) != CARQUET_REPETITION_REQUIRED;
        int16_t max_def = carquet_schema_node_max_def_level(node);

        int64_t rows_seen = 0;
        int64_t rows_output = 0;

        for (int32_t rg = 0; rg < num_rgs && rows_output < n; rg++) {
            carquet_row_group_metadata_t rgm;
            (void)carquet_reader_row_group_metadata(reader, rg, &rgm);

            /* Skip entire row groups before the start */
            if (rows_seen + rgm.num_rows <= skip_rows) {
                rows_seen += rgm.num_rows;
                continue;
            }

            carquet_column_reader_t* col = carquet_reader_get_column(reader, rg, c, &err);
            if (!col) continue;

            /* Skip rows within this row group */
            int64_t skip_in_rg = skip_rows - rows_seen;
            if (skip_in_rg < 0) skip_in_rg = 0;
            if (skip_in_rg > 0)
                carquet_column_skip(col, skip_in_rg);

            int64_t want = n - rows_output;
            int32_t elem_size = carquet_physical_type_size(phys);
            void* buf;
            int16_t* def = NULL;
            if (phys == CARQUET_PHYSICAL_BYTE_ARRAY) {
                buf = calloc((size_t)want, sizeof(carquet_byte_array_t));
            } else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY) {
                buf = calloc((size_t)want, (size_t)tl);
            } else {
                buf = calloc((size_t)want, (size_t)elem_size);
            }
            if (nullable)
                def = calloc((size_t)want, sizeof(int16_t));

            int64_t got = carquet_column_read_batch(col, buf, want, def, NULL);

            for (int64_t i = 0; i < got && rows_output < n; i++) {
                char vbuf[MAX_VALUE_BUF];
                if (nullable && def && def[i] < max_def) {
                    table_add_cell(&tbl, rows_output, c, "null");
                } else {
                    const void* vp = NULL;
                    if (phys == CARQUET_PHYSICAL_BYTE_ARRAY)
                        vp = &((carquet_byte_array_t*)buf)[i];
                    else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY)
                        vp = (uint8_t*)buf + i * tl;
                    else
                        vp = (uint8_t*)buf + i * elem_size;

                    cli_format_value(phys, vp, tl, lt, vbuf, sizeof(vbuf));
                    table_add_cell(&tbl, rows_output, c, vbuf);
                }
                rows_output++;
            }

            rows_seen += rgm.num_rows;
            free(buf);
            free(def);
            carquet_column_reader_free(col);
        }
    }

    table_print(&tbl);
    table_free(&tbl);
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_sample
 * ══════════════════════════════════════════════════════════════════════════ */

static int compare_int64(const void* a, const void* b) {
    int64_t va = *(const int64_t*)a;
    int64_t vb = *(const int64_t*)b;
    return (va > vb) - (va < vb);
}

int cmd_sample(const char* path, int64_t n) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int64_t total = carquet_reader_num_rows(reader);
    if (n > total) n = total;
    if (n <= 0 || num_cols <= 0) {
        carquet_reader_close(reader);
        return 0;
    }

    /* Generate n sorted random row indices using reservoir sampling.
     * For simplicity, just pick n random indices. */
    srand((unsigned)time(NULL));
    int64_t* indices = calloc((size_t)n, sizeof(int64_t));
    for (int64_t i = 0; i < n; i++) {
        indices[i] = ((int64_t)rand() * rand()) % total;
    }
    qsort(indices, (size_t)n, sizeof(int64_t), compare_int64);

    /* Remove duplicates */
    int64_t unique = 1;
    for (int64_t i = 1; i < n; i++) {
        if (indices[i] != indices[unique - 1])
            indices[unique++] = indices[i];
    }
    n = unique;

    /* Read sampled rows. For each column, we use head-style reading
     * with skip to jump to each sampled row. */
    table_t tbl;
    table_init(&tbl, schema, num_cols, n);

    for (int32_t c = 0; c < num_cols; c++) {
        carquet_physical_type_t phys = carquet_schema_column_type(schema, c);
        const carquet_schema_node_t* node = carquet_schema_get_element(schema,
            schema->leaf_indices[c]);
        const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
        int32_t tl = carquet_schema_node_type_length(node);
        bool nullable = carquet_schema_node_repetition(node) != CARQUET_REPETITION_REQUIRED;
        int16_t max_def = carquet_schema_node_max_def_level(node);
        int32_t num_rgs = carquet_reader_num_row_groups(reader);

        int64_t sample_idx = 0;     /* Index into sorted indices[] */
        int64_t rg_row_start = 0;   /* Absolute row offset of current row group */

        for (int32_t rg = 0; rg < num_rgs && sample_idx < n; rg++) {
            carquet_row_group_metadata_t rgm;
            (void)carquet_reader_row_group_metadata(reader, rg, &rgm);
            int64_t rg_row_end = rg_row_start + rgm.num_rows;

            /* Skip row groups with no sampled rows */
            if (sample_idx < n && indices[sample_idx] >= rg_row_end) {
                rg_row_start = rg_row_end;
                continue;
            }

            carquet_column_reader_t* col = carquet_reader_get_column(reader, rg, c, &err);
            if (!col) { rg_row_start = rg_row_end; continue; }

            int64_t pos_in_rg = 0; /* Current position within the row group */

            while (sample_idx < n && indices[sample_idx] < rg_row_end) {
                int64_t target_in_rg = indices[sample_idx] - rg_row_start;
                int64_t skip = target_in_rg - pos_in_rg;
                if (skip > 0) {
                    carquet_column_skip(col, skip);
                    pos_in_rg += skip;
                }

                /* Read one value */
                union {
                    int32_t i32; int64_t i64; float f; double d; uint8_t b;
                    carquet_byte_array_t ba;
                    uint8_t fixed[128];
                } val;
                int16_t def_level = 0;

                int64_t got = carquet_column_read_batch(col, &val, 1,
                    nullable ? &def_level : NULL, NULL);
                pos_in_rg++;

                char vbuf[MAX_VALUE_BUF];
                if (got <= 0) {
                    table_add_cell(&tbl, sample_idx, c, "?");
                } else if (nullable && def_level < max_def) {
                    table_add_cell(&tbl, sample_idx, c, "null");
                } else {
                    cli_format_value(phys, &val, tl, lt, vbuf, sizeof(vbuf));
                    table_add_cell(&tbl, sample_idx, c, vbuf);
                }

                sample_idx++;
            }

            carquet_column_reader_free(col);
            rg_row_start = rg_row_end;
        }
    }

    table_print(&tbl);
    table_free(&tbl);
    free(indices);
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Dynamic-width tabular printer
 *
 * Generic header + cells output that auto-sizes each column to the widest
 * value (capped at MAX_COL_WIDTH). Used by `cat` and `stat` so both commands
 * produce the same clean two-space-separated layout regardless of content
 * width. `cells` is a flat array indexed as cells[row * num_cols + col];
 * a NULL entry prints empty.
 * ══════════════════════════════════════════════════════════════════════════ */

static void print_dyn_table(const char* const* headers, int32_t num_cols,
                             const char* const* cells, int64_t num_rows) {
    if (num_cols <= 0) return;

    int* widths = calloc((size_t)num_cols, sizeof(int));
    if (!widths) return;

    for (int32_t c = 0; c < num_cols; c++) {
        int len = (int)strlen(headers[c]);
        widths[c] = len < MAX_COL_WIDTH ? len : MAX_COL_WIDTH;
    }
    for (int64_t r = 0; r < num_rows; r++) {
        for (int32_t c = 0; c < num_cols; c++) {
            const char* v = cells[r * num_cols + c];
            if (!v) continue;
            int len = (int)strlen(v);
            if (len > MAX_COL_WIDTH) len = MAX_COL_WIDTH;
            if (len > widths[c]) widths[c] = len;
        }
    }

    printf("  ");
    for (int32_t c = 0; c < num_cols; c++) {
        if (c > 0) printf("  ");
        printf("%-*.*s", widths[c], widths[c], headers[c]);
    }
    printf("\n  ");
    for (int32_t c = 0; c < num_cols; c++) {
        if (c > 0) printf("  ");
        for (int w = 0; w < widths[c]; w++) putchar('-');
    }
    printf("\n");

    for (int64_t r = 0; r < num_rows; r++) {
        printf("  ");
        for (int32_t c = 0; c < num_cols; c++) {
            const char* v = cells[r * num_cols + c];
            if (!v) v = "";
            if (c > 0) printf("  ");
            printf("%-*.*s", widths[c], widths[c], v);
        }
        printf("\n");
    }
    free(widths);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Shared row-extraction for cmd_cat and cmd_export
 *
 * Both commands need to read N rows starting at an offset, optionally
 * restricted to a column subset, and turn each value into a string. The
 * heavy lifting (per-column read + skip across row groups) lives here.
 * ══════════════════════════════════════════════════════════════════════════ */

/* Match `name` against the comma-separated list in `filter`. NULL filter
 * matches everything. Leading/trailing whitespace per token is tolerated. */
static bool name_in_filter(const char* name, const char* filter) {
    if (!filter) return true;
    const char* p = filter;
    size_t name_len = strlen(name);
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        const char* comma = strchr(p, ',');
        size_t tok_len = comma ? (size_t)(comma - p) : strlen(p);
        while (tok_len > 0 && (p[tok_len - 1] == ' ' || p[tok_len - 1] == '\t'))
            tok_len--;
        if (tok_len == name_len && strncmp(p, name, tok_len) == 0)
            return true;
        p = comma ? comma + 1 : p + strlen(p);
    }
    return false;
}

/* Resolve the column filter into a list of column indices. Returns the
 * number of selected columns, or -1 if a name didn't match the schema.
 * On success, *out is a malloc'd array the caller must free. */
static int32_t resolve_columns(const carquet_schema_t* schema,
                                int32_t num_cols, const char* filter,
                                int32_t** out) {
    int32_t* sel = malloc((size_t)num_cols * sizeof(int32_t));
    if (!sel) return -1;
    int32_t n = 0;
    for (int32_t c = 0; c < num_cols; c++) {
        const char* nm = carquet_schema_column_name(schema, c);
        if (name_in_filter(nm, filter)) {
            sel[n++] = c;
        }
    }
    if (filter && n == 0) {
        free(sel);
        return -1;
    }
    *out = sel;
    return n;
}

/* String matrix used to hold formatted values before display/export. */
typedef struct {
    char** cells;     /* [num_rows * num_cols], heap-strdup'd; may be NULL */
    int64_t num_rows;
    int32_t num_cols;
} str_matrix_t;

static void matrix_free(str_matrix_t* m) {
    if (m->cells) {
        int64_t total = m->num_rows * m->num_cols;
        for (int64_t i = 0; i < total; i++) free(m->cells[i]);
        free(m->cells);
    }
}

/* Read the requested column at `col_index`, skipping `offset` rows and
 * filling at most `limit` formatted strings into `matrix` at column slot
 * `dst_col`. Returns the number of rows actually filled. */
static int64_t read_column_strings(carquet_reader_t* reader,
                                    const carquet_schema_t* schema,
                                    int32_t col_index,
                                    int64_t offset, int64_t limit,
                                    str_matrix_t* matrix, int32_t dst_col) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_physical_type_t phys = carquet_schema_column_type(schema, col_index);
    const carquet_schema_node_t* node = carquet_schema_get_element(schema,
        schema->leaf_indices[col_index]);
    const carquet_logical_type_t* lt = carquet_schema_node_logical_type(node);
    int32_t tl = carquet_schema_node_type_length(node);
    bool nullable = carquet_schema_node_repetition(node) != CARQUET_REPETITION_REQUIRED;
    int16_t max_def = carquet_schema_node_max_def_level(node);

    int32_t num_rgs = carquet_reader_num_row_groups(reader);
    int64_t rows_seen = 0;
    int64_t rows_output = 0;

    for (int32_t rg = 0; rg < num_rgs && rows_output < limit; rg++) {
        carquet_row_group_metadata_t rgm;
        (void)carquet_reader_row_group_metadata(reader, rg, &rgm);

        if (rows_seen + rgm.num_rows <= offset) {
            rows_seen += rgm.num_rows;
            continue;
        }

        carquet_column_reader_t* col = carquet_reader_get_column(reader, rg,
            col_index, &err);
        if (!col) { rows_seen += rgm.num_rows; continue; }

        int64_t skip_in_rg = offset - rows_seen;
        if (skip_in_rg < 0) skip_in_rg = 0;
        if (skip_in_rg > 0) carquet_column_skip(col, skip_in_rg);

        int64_t want = limit - rows_output;
        int64_t rg_remaining = rgm.num_rows - skip_in_rg;
        if (want > rg_remaining) want = rg_remaining;

        int32_t elem_size = carquet_physical_type_size(phys);
        void* buf;
        int16_t* def = NULL;
        if (phys == CARQUET_PHYSICAL_BYTE_ARRAY)
            buf = calloc((size_t)want, sizeof(carquet_byte_array_t));
        else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY)
            buf = calloc((size_t)want, (size_t)tl);
        else
            buf = calloc((size_t)want, (size_t)elem_size);
        if (nullable) def = calloc((size_t)want, sizeof(int16_t));

        int64_t got = carquet_column_read_batch(col, buf, want, def, NULL);

        for (int64_t i = 0; i < got && rows_output < limit; i++) {
            char vbuf[MAX_VALUE_BUF];
            const char* cell;
            if (nullable && def && def[i] < max_def) {
                cell = "";
            } else {
                const void* vp;
                if (phys == CARQUET_PHYSICAL_BYTE_ARRAY)
                    vp = &((carquet_byte_array_t*)buf)[i];
                else if (phys == CARQUET_PHYSICAL_FIXED_LEN_BYTE_ARRAY)
                    vp = (uint8_t*)buf + i * tl;
                else
                    vp = (uint8_t*)buf + i * elem_size;
                cli_format_value(phys, vp, tl, lt, vbuf, sizeof(vbuf));
                cell = vbuf;
            }
            matrix->cells[rows_output * matrix->num_cols + dst_col] =
                carquet_heap_strdup(cell);
            rows_output++;
        }

        rows_seen += rgm.num_rows;
        free(buf);
        free(def);
        carquet_column_reader_free(col);
    }

    return rows_output;
}

static int read_rows(carquet_reader_t* reader,
                     const carquet_schema_t* schema,
                     const int32_t* col_indices, int32_t num_sel_cols,
                     int64_t offset, int64_t limit,
                     str_matrix_t* out) {
    out->num_cols = num_sel_cols;
    out->num_rows = limit;
    out->cells = calloc((size_t)(limit * num_sel_cols), sizeof(char*));
    if (!out->cells) return -1;

    int64_t produced = 0;
    for (int32_t i = 0; i < num_sel_cols; i++) {
        int64_t n = read_column_strings(reader, schema, col_indices[i],
                                         offset, limit, out, i);
        if (n > produced) produced = n;
    }
    out->num_rows = produced;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_cat — print rows with optional slicing and column filter
 * ══════════════════════════════════════════════════════════════════════════ */

int cmd_cat(const char* path, const row_select_opts_t* opts) {
    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int64_t total = carquet_reader_num_rows(reader);

    int64_t offset = opts->offset < 0 ? 0 : opts->offset;
    if (offset > total) offset = total;
    int64_t limit = opts->limit < 0 ? (total - offset) : opts->limit;
    if (limit > total - offset) limit = total - offset;

    int32_t* sel = NULL;
    int32_t num_sel = resolve_columns(schema, num_cols, opts->columns, &sel);
    if (num_sel < 0) {
        fprintf(stderr, "error: no columns matched filter '%s'\n",
                opts->columns ? opts->columns : "");
        free(sel);
        carquet_reader_close(reader);
        return 1;
    }
    if (limit <= 0 || num_sel == 0) {
        free(sel);
        carquet_reader_close(reader);
        return 0;
    }

    str_matrix_t mat = {0};
    if (read_rows(reader, schema, sel, num_sel, offset, limit, &mat) != 0) {
        free(sel);
        carquet_reader_close(reader);
        return 1;
    }

    const char** headers = malloc((size_t)num_sel * sizeof(const char*));
    for (int32_t c = 0; c < num_sel; c++) {
        headers[c] = carquet_schema_column_name(schema, sel[c]);
    }
    print_dyn_table(headers, num_sel, (const char* const*)mat.cells, mat.num_rows);
    free(headers);

    matrix_free(&mat);
    free(sel);
    carquet_reader_close(reader);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * cmd_export --format csv — write rows to stdout as CSV
 *
 * RFC 4180 quoting: fields containing comma, quote, CR, or LF are wrapped
 * in double quotes; embedded quotes are doubled. Header row first.
 * ══════════════════════════════════════════════════════════════════════════ */

static void emit_csv_field(const char* v) {
    if (!v) v = "";
    bool needs_quote = false;
    for (const char* p = v; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quote = true;
            break;
        }
    }
    if (!needs_quote) {
        fputs(v, stdout);
        return;
    }
    fputc('"', stdout);
    for (const char* p = v; *p; p++) {
        if (*p == '"') fputc('"', stdout);
        fputc(*p, stdout);
    }
    fputc('"', stdout);
}

int cmd_export(const char* path, const row_select_opts_t* opts, export_format_t fmt) {
    if (fmt != CLI_EXPORT_CSV) {
        fprintf(stderr, "error: unsupported export format\n");
        return 1;
    }

    carquet_error_t err = CARQUET_ERROR_INIT;
    carquet_reader_t* reader = open_or_die(path, &err);
    if (!reader) return 1;

    const carquet_schema_t* schema = carquet_reader_schema(reader);
    int32_t num_cols = carquet_reader_num_columns(reader);
    int64_t total = carquet_reader_num_rows(reader);

    int64_t offset = opts->offset < 0 ? 0 : opts->offset;
    if (offset > total) offset = total;
    int64_t limit = opts->limit < 0 ? (total - offset) : opts->limit;
    if (limit > total - offset) limit = total - offset;

    int32_t* sel = NULL;
    int32_t num_sel = resolve_columns(schema, num_cols, opts->columns, &sel);
    if (num_sel < 0) {
        fprintf(stderr, "error: no columns matched filter '%s'\n",
                opts->columns ? opts->columns : "");
        free(sel);
        carquet_reader_close(reader);
        return 1;
    }

    /* Header row (always emitted, even when limit==0). */
    for (int32_t c = 0; c < num_sel; c++) {
        if (c > 0) fputc(',', stdout);
        emit_csv_field(carquet_schema_column_name(schema, sel[c]));
    }
    fputc('\n', stdout);

    if (limit <= 0 || num_sel == 0) {
        free(sel);
        carquet_reader_close(reader);
        return 0;
    }

    str_matrix_t mat = {0};
    if (read_rows(reader, schema, sel, num_sel, offset, limit, &mat) != 0) {
        free(sel);
        carquet_reader_close(reader);
        return 1;
    }

    for (int64_t r = 0; r < mat.num_rows; r++) {
        for (int32_t c = 0; c < num_sel; c++) {
            if (c > 0) fputc(',', stdout);
            emit_csv_field(mat.cells[r * num_sel + c]);
        }
        fputc('\n', stdout);
    }

    matrix_free(&mat);
    free(sel);
    carquet_reader_close(reader);
    return 0;
}
