#!/usr/bin/env python3
"""Generate a WIT planning scaffold from Carquet's public C headers.

The generated output is intentionally a scaffold, not final bindings. It keeps
the raw C signatures visible while classifying functions into likely component
API buckets so the component layer can grow ergonomic APIs without drifting from
the link-level C surface used by native/Pascal consumers.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
from pathlib import Path


API_DECL_RE = re.compile(
    r"\bCARQUET_API\b(?P<body>.*?)(?P<name>carquet_[A-Za-z0-9_]+)\s*"
    r"\((?P<params>.*?)\)\s*;",
    re.DOTALL,
)
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
SPACE_RE = re.compile(r"\s+")


@dataclasses.dataclass(frozen=True)
class Function:
    name: str
    return_type: str
    params: str
    bucket: str


BUCKETS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("lifecycle", ("init", "cleanup", "version", "allocator")),
    ("schema", ("schema",)),
    ("reader", ("reader", "row_batch", "batch_reader", "column_reader")),
    ("writer", ("writer",)),
    ("metadata", ("metadata", "statistics", "bloom", "page_index", "column_index", "offset_index")),
    ("types", ("logical_type", "compression", "encoding", "physical_type", "status")),
    ("simd", ("cpu", "simd")),
)


def clean(text: str) -> str:
    text = COMMENT_RE.sub(" ", text)
    return SPACE_RE.sub(" ", text).strip()


def bucket_for(name: str) -> str:
    stem = name.removeprefix("carquet_")
    for bucket, needles in BUCKETS:
        if any(needle in stem for needle in needles):
            return bucket
    return "misc"


def parse_header(header: Path) -> list[Function]:
    text = header.read_text(encoding="utf-8")
    text = COMMENT_RE.sub(" ", text)
    text = "\n".join(
        line for line in text.splitlines()
        if not line.lstrip().startswith("#")
    )
    functions: list[Function] = []
    for match in API_DECL_RE.finditer(text):
        body = clean(match.group("body"))
        name = match.group("name")
        params = clean(match.group("params"))
        return_type = body
        functions.append(Function(name, return_type, params, bucket_for(name)))
    return sorted(functions, key=lambda item: (item.bucket, item.name))


def wit_name(c_name: str) -> str:
    return c_name.removeprefix("carquet_").replace("_", "-")


def write_markdown(functions: list[Function], out: Path) -> None:
    by_bucket: dict[str, list[Function]] = {}
    for function in functions:
        by_bucket.setdefault(function.bucket, []).append(function)

    lines = [
        "# Carquet C API WIT Scaffold",
        "",
        "Generated from `include/carquet/carquet.h`.",
        "",
        "Use this as the planning inventory for the component-model API. The C",
        "signatures remain the link-level source of truth; WIT exports can wrap",
        "these with handles, records, results, lists, and ergonomic helpers.",
        "",
    ]

    for bucket in sorted(by_bucket):
        lines.append(f"## {bucket}")
        lines.append("")
        lines.append("| C symbol | Candidate WIT name | C signature |")
        lines.append("| --- | --- | --- |")
        for function in by_bucket[bucket]:
            signature = f"{function.return_type} {function.name}({function.params})"
            lines.append(f"| `{function.name}` | `{wit_name(function.name)}` | `{signature}` |")
        lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")


def write_wit_draft(functions: list[Function], out: Path) -> None:
    by_bucket: dict[str, list[Function]] = {}
    for function in functions:
        by_bucket.setdefault(function.bucket, []).append(function)

    lines = [
        "package simul8:carquet-draft;",
        "",
        "/// Draft inventory generated from the public C API.",
        "/// Replace raw pointer APIs with resources and result types before use.",
        "world carquet-draft {",
    ]
    for bucket in sorted(by_bucket):
        lines.append(f"  /// {bucket}")
        for function in by_bucket[bucket]:
            lines.append(f"  // {function.return_type} {function.name}({function.params});")
            lines.append(f"  // export {wit_name(function.name)}: func();")
    lines.append("}")
    lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--header",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "include/carquet/carquet.h",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "Core/Api/.deps/carquet-wit-scaffold",
    )
    args = parser.parse_args()

    functions = parse_header(args.header)
    write_markdown(functions, args.out_dir / "carquet-c-api.md")
    write_wit_draft(functions, args.out_dir / "carquet-draft.wit")
    print(f"{len(functions)} functions -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
