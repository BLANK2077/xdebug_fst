#!/usr/bin/env python3
"""Convert an ABI-v2 XDD shared library into binary DesignDB v1."""

from __future__ import annotations

import argparse
import ctypes
import json
import struct
from pathlib import Path

MAGIC = b"XDDBIN1\0"
HEADER_PREFIX = struct.Struct("<8sIIIIQ")
SECTION = struct.Struct("<QQ")
SIGNAL = struct.Struct("<IIIiii")
NAME_INDEX = struct.Struct("<Ii")
DRIVER = struct.Struct("<iiIIIIi")
LOAD = struct.Struct("<iiIIi")
PORT = struct.Struct("<iiI")
START = struct.Struct("<Q")
SECTION_COUNT = 9
HEADER_SIZE = HEADER_PREFIX.size + SECTION.size * SECTION_COUNT


class ConversionError(RuntimeError):
    pass


def _text(value: bytes | None) -> str:
    return value.decode("utf-8") if value else ""


class Strings:
    def __init__(self) -> None:
        self.data = bytearray(b"\0")
        self.offsets = {"": 0}

    def intern(self, value: str) -> int:
        found = self.offsets.get(value)
        if found is not None:
            return found
        encoded = value.encode("utf-8") + b"\0"
        offset = len(self.data)
        if offset > 0xFFFFFFFF or len(self.data) + len(encoded) > 0x100000000:
            raise ConversionError("string pool exceeds binary-v1 32-bit offset limit")
        self.data.extend(encoded)
        self.offsets[value] = offset
        return offset


def _configure(library: ctypes.CDLL) -> None:
    library.xdd_abi_version.restype = ctypes.c_int
    library.xdd_capabilities.restype = ctypes.c_uint64
    library.xdd_init.restype = ctypes.c_void_p
    library.xdd_close.argtypes = [ctypes.c_void_p]
    for name in ("xdd_signal_count", "xdd_signal_width", "xdd_signal_line",
                 "xdd_signal_direction", "xdd_trace_driver_count",
                 "xdd_trace_load_count", "xdd_port_connection_count"):
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p, ctypes.c_int] if name != "xdd_signal_count" else [ctypes.c_void_p]
        function.restype = ctypes.c_int
    library.xdd_resolve.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    library.xdd_resolve.restype = ctypes.c_int
    for name in ("xdd_signal_name", "xdd_signal_type", "xdd_signal_file"):
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p, ctypes.c_int]
        function.restype = ctypes.c_char_p
    for name in ("xdd_trace_driver_role", "xdd_trace_driver_predicate"):
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        function.restype = ctypes.c_char_p
    library.xdd_trace_driver.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_char_p),
        ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_int),
    ]
    library.xdd_trace_load.argtypes = library.xdd_trace_driver.argtypes
    library.xdd_port_connection.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_char_p),
    ]


def _pack_sections(blobs: list[tuple[bytes, int]]) -> bytes:
    offset = HEADER_SIZE
    locations: list[tuple[int, int]] = []
    body = bytearray()
    for blob, count in blobs:
        padding = (-offset) % 8
        body.extend(b"\0" * padding)
        offset += padding
        locations.append((offset, count))
        body.extend(blob)
        offset += len(blob)
    header = HEADER_PREFIX.pack(MAGIC, 1, 0, 0x01020304, HEADER_SIZE, offset)
    header += b"".join(SECTION.pack(*location) for location in locations)
    if len(header) != HEADER_SIZE:
        raise AssertionError("binary DesignDB header size mismatch")
    return header + body


def convert(source: Path, output: Path) -> dict[str, int]:
    library = ctypes.CDLL(str(source.resolve()))
    _configure(library)
    if library.xdd_abi_version() != 2 or library.xdd_capabilities() & 15 != 15:
        raise ConversionError("source XDD library lacks required ABI-v2 capabilities")
    database = library.xdd_init()
    if not database:
        raise ConversionError("source XDD library initialization failed")
    strings = Strings()
    signal_blob = bytearray()
    driver_blob = bytearray()
    load_blob = bytearray()
    port_blob = bytearray()
    driver_starts = bytearray()
    load_starts = bytearray()
    port_starts = bytearray()
    names: list[tuple[str, int]] = []
    try:
        count = library.xdd_signal_count(database)
        for index in range(count):
            name = _text(library.xdd_signal_name(database, index))
            names.append((name, index))
            signal_blob.extend(SIGNAL.pack(
                strings.intern(name),
                strings.intern(_text(library.xdd_signal_type(database, index))),
                strings.intern(_text(library.xdd_signal_file(database, index))),
                library.xdd_signal_width(database, index),
                library.xdd_signal_line(database, index),
                library.xdd_signal_direction(database, index),
            ))
            driver_starts.extend(START.pack(len(driver_blob) // DRIVER.size))
            for ordinal in range(library.xdd_trace_driver_count(database, index)):
                related = ctypes.c_int(-1)
                kind = ctypes.c_char_p()
                file_name = ctypes.c_char_p()
                line = ctypes.c_int(0)
                library.xdd_trace_driver(database, index, ordinal, ctypes.byref(related),
                    ctypes.byref(kind), ctypes.byref(file_name), ctypes.byref(line))
                driver_blob.extend(DRIVER.pack(
                    index, related.value, strings.intern(_text(kind.value)),
                    strings.intern(_text(library.xdd_trace_driver_role(database, index, ordinal))),
                    strings.intern(_text(library.xdd_trace_driver_predicate(database, index, ordinal))),
                    strings.intern(_text(file_name.value)), line.value,
                ))
            load_starts.extend(START.pack(len(load_blob) // LOAD.size))
            for ordinal in range(library.xdd_trace_load_count(database, index)):
                related = ctypes.c_int(-1)
                kind = ctypes.c_char_p()
                file_name = ctypes.c_char_p()
                line = ctypes.c_int(0)
                library.xdd_trace_load(database, index, ordinal, ctypes.byref(related),
                    ctypes.byref(kind), ctypes.byref(file_name), ctypes.byref(line))
                load_blob.extend(LOAD.pack(index, related.value,
                    strings.intern(_text(kind.value)), strings.intern(_text(file_name.value)), line.value))
            port_starts.extend(START.pack(len(port_blob) // PORT.size))
            for ordinal in range(library.xdd_port_connection_count(database, index)):
                related = ctypes.c_int(-1)
                kind = ctypes.c_char_p()
                library.xdd_port_connection(database, index, ordinal,
                    ctypes.byref(related), ctypes.byref(kind))
                port_blob.extend(PORT.pack(index, related.value, strings.intern(_text(kind.value))))
        driver_starts.extend(START.pack(len(driver_blob) // DRIVER.size))
        load_starts.extend(START.pack(len(load_blob) // LOAD.size))
        port_starts.extend(START.pack(len(port_blob) // PORT.size))
        names.sort(key=lambda item: item[0])
        name_blob = b"".join(NAME_INDEX.pack(strings.intern(name), index) for name, index in names)
        blobs = [
            (bytes(signal_blob), count), (name_blob, count),
            (bytes(driver_blob), len(driver_blob) // DRIVER.size),
            (bytes(driver_starts), count + 1),
            (bytes(load_blob), len(load_blob) // LOAD.size),
            (bytes(load_starts), count + 1),
            (bytes(port_blob), len(port_blob) // PORT.size),
            (bytes(port_starts), count + 1), (bytes(strings.data), len(strings.data)),
        ]
        payload = _pack_sections(blobs)
        output.write_bytes(payload)
        return {
            "signals": count,
            "drivers": len(driver_blob) // DRIVER.size,
            "loads": len(load_blob) // LOAD.size,
            "ports": len(port_blob) // PORT.size,
            "strings_bytes": len(strings.data),
            "file_bytes": len(payload),
        }
    finally:
        library.xdd_close(database)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--write-manifest", action="store_true",
        help="also create a strict binary-v1 bundle manifest beside output",
    )
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error("source must be an existing XDD shared library")
    if args.output.exists():
        parser.error("output already exists")
    manifest = args.output.parent / "xdebug-design-db.json"
    if args.write_manifest and manifest.exists():
        parser.error("bundle manifest already exists")
    stats = convert(args.source, args.output)
    if args.write_manifest:
        manifest.write_text(json.dumps({
            "schema_version": "xdebug.design-db-bundle.v2",
            "format": "binary-v1",
            "database": args.output.name,
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(" ".join(f"{key}={value}" for key, value in stats.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
