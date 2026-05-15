# Changelog

## v0.4.4

### Bug Fixes

- **Fixed page checksum interoperability**: Page CRCs now use standard IEEE CRC32 as required by the Parquet spec, so files validate with readers that enable page checksum verification.
- **Fixed LZ4 writer metadata**: `CARQUET_COMPRESSION_LZ4` writer requests are normalized to the interoperable Parquet `LZ4_RAW` codec because carquet writes raw LZ4 blocks, not the deprecated framed LZ4 format.
- **Fixed Bloom filter block selection**: Split block Bloom filters now use the Parquet-specified block selection formula.
- **Tightened schema validation**: Primitive logical type annotations are now validated against their allowed physical types and parameters before being added to a schema.
- **Added newer logical type metadata**: The writer/parser now supports Parquet `VARIANT`, `GEOMETRY`, and `GEOGRAPHY` logical annotations, including geography edge interpolation metadata.
- **Added column order metadata and stricter statistics semantics**: File metadata now emits `column_orders`, unsigned integer statistics use unsigned ordering, floating-point statistics ignore NaN values, and undefined-order logical types suppress min/max statistics.

### Performance

- **Hardware-accelerated page checksums**: The IEEE CRC32 used for page checksums now routes through zlib's `crc32`, which ships PCLMULQDQ-folding on x86 and FEAT_CRC32 on ARMv8 instead of a scalar slicing-by-8 loop. CRC is computed for every page on both write and read, so this lifts the whole pipeline — most dramatically for fast codecs where the checksum dominated (e.g. `large/snappy` read ~160ms → ~22ms, write ~824ms → ~231ms on Apple M3). Removes the now-unused CRC32C SIMD code paths.
- **Re-fused statistics collection**: Fixed-width PLAIN encoding again computes column min/max in the same SIMD pass that copies values into the page buffer, instead of a separate scalar pass, halving the memory traffic of the write hot path. Float/double min/max use the SIMD path with a NaN-skipping rescan only when NaNs are present.

### API

- Public API added `CARQUET_LOGICAL_VARIANT`, `CARQUET_LOGICAL_GEOMETRY`, `CARQUET_LOGICAL_GEOGRAPHY`, geospatial edge algorithm constants, and `carquet_schema_add_variant()`. Existing `CARQUET_COMPRESSION_LZ4` writer usage is accepted, but emitted metadata now uses `LZ4_RAW`.

## v0.4.3

### Bug Fixes

- **Restored Parquet logical type backward compatibility**: Writer metadata now includes the legacy `ConvertedType` annotations required by older readers when a matching modern `LogicalType` is present, including string, date/time, timestamp, decimal, integer, JSON/BSON, enum, list, and map annotations. Decimal scale/precision are also mirrored into the legacy schema fields, and old files that only contain `ConvertedType` are normalized to carquet logical types when read.

### API

- No public API changes.

## v0.4.2

### Bug Fixes

- **Statistics are now actually written to Parquet files**: `write_statistics = true` (the default) was silently being ignored — page-level min/max/null counts were never propagated up into the column chunk metadata, so files contained no statistics regardless of the option. Column stats now flow correctly from page → column → row group → file metadata. Existing files written with older versions are unaffected; only the writer was broken.
- **Fixed `carquet stat` CLI segfault on string columns**: The `stat` subcommand crashed when displaying min/max for `BYTE_ARRAY` (string) columns. The bug was latent until statistics actually started being written.
- **Fixed nullable value decoding contract**: Optional columns now consistently use a packed non-null value stream plus definition levels, including `BYTE_ARRAY`, dictionary pages, partial page reads, and generated reader code.
- **Fixed null bitmap polarity**: Batch reader null bitmaps now consistently follow the documented convention: bit set means the value is present.
- **Fixed compressed mmap batch-reader edge cases**: The pipeline no longer drops null information for optional/repeated columns, and intra-column split tasks no longer share mutable column-reader scratch buffers.
- **Rejected unsupported writer encodings**: Per-column encoding overrides now fail fast for encodings the writer cannot actually emit, avoiding mismatched metadata and payloads.

### New Features

- **Statistics for all primitive types**: Min/max/null-count are now tracked for `BOOLEAN`, `BYTE_ARRAY`, and `FIXED_LEN_BYTE_ARRAY` in addition to the numeric types. Byte-array min/max use Parquet-spec lexicographic ordering with min/max truncation at 32 bytes for long values (max is incremented so the stored bound remains a valid upper bound).
- **New CLI commands**:
  - `carquet cat [-n LIMIT] [-s OFFSET] [-c COLS] <file>` — print rows with arbitrary slicing and column filtering. Fills the gap between `head`/`tail` (anchored to start/end) and `sample` (random).
  - `carquet export [--format csv] [-n LIMIT] [-s OFFSET] [-c COLS] <file>` — write rows to stdout as RFC 4180 CSV with the same slicing/filter options. Useful for piping into shell tools or other CSV consumers.
- **Cleaner `carquet stat` output**: columns are now auto-sized to their actual content width instead of fixed 20/30-character cells, so short numeric stats no longer create huge empty gaps and long string min/max are no longer truncated.

## v0.4.1

- Hardened page reader bounds checks for mmap and fread paths, including page payload spans, page sizes, and offset arithmetic.
- Fixed compressed Data Page V2 handling in the fread reader path by decompressing only the compressed data section while preserving uncompressed level bytes.
- Improved malformed-input resistance with checked allocation/growth arithmetic in core buffer and arena helpers.
- Tightened batch-reader coalesced reads so unsupported or suspicious page layouts fall back to the standard page reader.
- Made page writer value appends transactional on encode failure, preventing partial page state from leaking into later writes.

## v0.4.0

### New Features

- **CLI tool (`carquet`)**: Ships a built-in command-line tool for inspecting Parquet files and generating reader code. Built by default (`CARQUET_BUILD_CLI=ON`), installed globally with `make install`.
  - `schema` — print file schema
  - `info` — print detailed file metadata
  - `head` / `tail` — print first/last N rows
  - `count` — print total row count
  - `columns` — list column names (one per line)
  - `stat` — print column statistics
  - `validate` — verify file integrity
  - `sample` — print N random rows
  - `codegen` — generate C reader code from a Parquet file's schema
  - All subcommands support `-h` / `--help`.

- **Code generation (`carquet codegen`)**: Reads a real Parquet file's schema and generates a complete, compilable C program tailored to that schema.
  - `-f` / `--file` — input Parquet file to inspect (generates a placeholder path if omitted)
  - `-o` / `--output` — output source file (prints compile command on stderr)
  - `-b` / `--batch-size` — batch size in generated code
  - `-c` / `--columns` — comma-separated column filter
  - `--mmap` — generate memory-mapped I/O reader
  - `--skeleton` — generate empty `process_batch` body for custom logic
  - Auto-detects compiler (respects `$CC`), carquet include/lib paths, and link dependencies
  - Embeds the source Parquet file as default input so the generated binary works without arguments
  - Generated code compiles with zero warnings

- **Versioned manual in `docs/`**: Added focused in-repo documentation for the main workflows and API concepts.
  - `docs/README.md` — manual index and API surface guide
  - `docs/reading.md` — reader setup, batch scans, column reads, filtering, metadata inspection
  - `docs/writing.md` — schema creation, required/nullable writes, row groups, buffer writer
  - `docs/nested-data.md` — groups, lists, maps, definition levels, repetition levels
  - `docs/performance.md` — mmap, zero-copy, dictionary-preserving reads, prebuffering, tuning
  - `docs/error-handling.md` — status codes, rich error context, type mapping, and level/null conventions

- **Row group predicate pushdown in batch reader**: Added `row_group_filter` callback to `carquet_batch_reader_config_t` for zero-I/O elimination of non-matching row groups using column statistics.
- **I/O coalescing**: Added `carquet_reader_prebuffer()` to pre-read multiple column chunks in a single coalesced read.
- **Speculative footer read**: File open reads up to 64KB from the end in a single I/O call, reducing the open path from 3 I/O calls to 2 for most files.
- **Data Page V2 decoding**: Page reader support for Parquet Data Page V2.
- **Write-path profiling target**: Added the `profile_write` binary for dedicated write-path profiling.

### New APIs

- **Bloom filter query**: Read bloom filters from Parquet files and check value membership — enables column-chunk-level predicate pushdown.
  `carquet_reader_get_bloom_filter()`, `carquet_bloom_filter_check_i32/i64/float/double/bytes()`, `carquet_bloom_filter_size()`, `carquet_bloom_filter_destroy()`
- **Page index read**: Access per-page min/max statistics (column index) and page file locations (offset index) — enables page-level predicate pushdown, skipping individual pages within a column chunk.
  `carquet_reader_get_column_index()`, `carquet_column_index_num_pages()`, `carquet_column_index_get_page_stats()`, `carquet_reader_get_offset_index()`, `carquet_offset_index_get_page_location()`
- **Key-value metadata**: Read and write arbitrary string key-value pairs in the Parquet footer (used by Pandas, Arrow, Spark for schema annotations).
  `carquet_reader_num_metadata()`, `carquet_reader_get_metadata()`, `carquet_reader_find_metadata()`, `carquet_writer_add_metadata()`
- **Column chunk metadata**: Inspect per-column-per-row-group details: codec, encoding, sizes, and which optional features (bloom filter, page index) are present.
  `carquet_reader_column_chunk_metadata()`
- **Per-column writer options**: Override global encoding, compression, statistics, and bloom filter settings on a per-column basis.
  `carquet_writer_set_column_encoding()`, `carquet_writer_set_column_compression()`, `carquet_writer_set_column_statistics()`, `carquet_writer_set_column_bloom_filter()`
- **Buffer writer**: Write Parquet data to an in-memory buffer instead of a file — useful for network protocols, embedding, and testing.
  `carquet_writer_create_buffer()`, `carquet_writer_get_buffer()`

### Performance

- **Multi-row-group pipeline decompression**: Persistent worker pool with pipeline ring buffer for parallel bulk-reads. On 10M-row / 10-RG / 3-column benchmark (Apple M3): snappy 16ms (was 40ms), zstd 25ms (was 44ms), lz4 12ms (was 26ms).
- **ZSTD thread safety**: Per-thread `ZSTD_DCtx`/`ZSTD_CCtx` cache via `pthread_key_create` on all POSIX builds.
- **ARM NEON byte-stream-split**: Widened AArch64 double encode/decode hot loop to 4 doubles at a time.
- **Cheaper page-load peeks**: Zero-length batch reads share the page-loader helper.

### Bug Fixes

- **Fixed BYTE_ARRAY nullable column reads**: Writer now encodes all `num_values` entries for BYTE_ARRAY columns (including zero-length entries for nulls) so values stay aligned with definition levels. Reader's PLAIN decoder and dictionary lookup paths updated to match.
- **Fixed dictionary-encoded nullable columns**: Dictionary index decoding and lookup now process `num_values` entries instead of `non_null_count`.

### Internal

- CLI sources in `src/cli/`: `main.c`, `commands.c`, `codegen.c`, `codegen_read.c`, `codegen_write.c` (stub).
- Build option `CARQUET_BUILD_CLI` (default ON), installs `carquet` binary alongside the library.
- Batch reader pipeline serves pre-read data via zero-copy views from ring buffer slots.
- Worker pool queue capacity increased to 512 for cross-RG bulk-read task submission.
- Page reader fread path uses `prebuf_read_at()` helper for prebuffer cache.
- Windows compatibility: `_getcwd`, `_access`, `_fullpath`, `gmtime_s` behind `#ifdef _WIN32`.

## v0.3.1

- Snappy compression updates and fuzzing improvements.

## v0.3.1_2

- Minor build fix.

## v0.3.1_1

- Minor build fix.

## v0.3.0_6

- Windows build fixes.
