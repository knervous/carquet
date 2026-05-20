# carquet component interface

This directory contains the WIT package used for the standalone WASI Preview 2
component build.

`carquet.wit` is the current minimal probe world that is compiled into the
standalone component by `ensure-carquet.sh`.

The header-derived API scaffold lives in `../wit-api/carquet-api.wit`. That
package tracks the public C API in `include/carquet/carquet.h` as an ergonomic
component surface: resource handles for opaque C pointers, `result` error
returns, byte-list buffers instead of `FILE*` or filesystem paths, and typed
records for Carquet metadata. It intentionally omits allocator callbacks,
`FILE*`, path-based file I/O, direct thread-pool control, and deprecated/native
only entry points from the component layer.

The scaffold also includes a `helper-api` interface mirroring the host helper
wrappers in `include/carquet/carquet.h`: open/save with options, save to buffer,
cell update as an input-to-output rewrite operation, and rectangular block
retrieval.

Generate C guest bindings with:

```sh
./generate-c-bindings.sh
```

By default the generated files are written outside the source tree at:

```text
../Core/Api/.deps/carquet-bindings/c
```

The component build script also generates these bindings into its own build
directory and links `carquet_component_type.o` into the final component.

Validate the API scaffold without changing the live component build with:

```sh
wit-bindgen c --world carquet-api --out-dir ../Core/Api/.deps/carquet-api-bindings/c ../carquet/wit-api
```
