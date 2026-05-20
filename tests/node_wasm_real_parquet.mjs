import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const [wasmPath, parquetPath] = process.argv.slice(2);

if (!wasmPath || !parquetPath) {
  console.error('usage: node node_wasm_real_parquet.mjs <probe.wasm> <file.parquet>');
  process.exit(2);
}

const wasi = new WASI({
  version: 'preview1',
  args: ['carquet-wasm-real-parquet-probe'],
  env: {},
  preopens: {},
});

const [wasmBytes, parquetBytes] = await Promise.all([
  readFile(wasmPath),
  readFile(parquetPath),
]);

const module = await WebAssembly.compile(wasmBytes);
const instance = await WebAssembly.instantiate(module, {
  wasi_snapshot_preview1: wasi.wasiImport,
});

const { memory, malloc, free, carquet_wasm_real_parquet_probe: probe } = instance.exports;
if (!memory || !malloc || !free || !probe) {
  throw new Error('probe wasm is missing memory, malloc, free, or probe export');
}

const ptr = malloc(parquetBytes.byteLength);
if (!ptr) {
  throw new Error('wasm malloc returned null');
}

try {
  new Uint8Array(memory.buffer, ptr, parquetBytes.byteLength).set(parquetBytes);
  const packed = probe(ptr, parquetBytes.byteLength);
  const rows = Number((packed >> 32n) & 0xffffffffn);
  const status = Number((packed >> 16n) & 0xffffn);
  const columns = Number(packed & 0xffffn);

  if (status !== 0) {
    throw new Error(`carquet wasm probe failed with status ${status}`);
  }

  const expectedRows = process.env.CARQUET_EXPECT_ROWS;
  const expectedColumns = process.env.CARQUET_EXPECT_COLUMNS;
  if (expectedRows && rows !== Number(expectedRows)) {
    throw new Error(`unexpected row count rows=${rows} expected=${expectedRows}`);
  }
  if (expectedColumns && columns !== Number(expectedColumns)) {
    throw new Error(`unexpected column count columns=${columns} expected=${expectedColumns}`);
  }
  if (!expectedRows && !expectedColumns && rows === 3 && columns === 2 && parquetBytes.byteLength === 366) {
    // Default fixture path.
  } else if (rows < 0 || columns < 0) {
    throw new Error(`unexpected parquet shape rows=${rows} columns=${columns}`);
  }

  console.log(JSON.stringify({ file: parquetPath, rows, columns, bytes: parquetBytes.byteLength }));
} finally {
  free(ptr);
}
