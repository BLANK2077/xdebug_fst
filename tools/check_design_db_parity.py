#!/usr/bin/env python3
"""Compare every published fact in an ABI-v2 XDD SO and binary DesignDB v1."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

from convert_xdd_so_to_binary import (
    DRIVER, HEADER_PREFIX, HEADER_SIZE, LOAD, MAGIC, NAME_INDEX, PORT, SECTION,
    SIGNAL, START, _configure, _text,
)


class ParityError(RuntimeError):
    pass


def so_snapshot(path: Path) -> dict[str, Any]:
    library = ctypes.CDLL(str(path.resolve()))
    _configure(library)
    database = library.xdd_init()
    if not database:
        raise ParityError("source XDD library initialization failed")
    signals: list[tuple[Any, ...]] = []
    drivers: list[list[tuple[Any, ...]]] = []
    loads: list[list[tuple[Any, ...]]] = []
    ports: list[list[tuple[Any, ...]]] = []
    try:
        count = library.xdd_signal_count(database)
        for index in range(count):
            signals.append((
                _text(library.xdd_signal_name(database, index)),
                _text(library.xdd_signal_type(database, index)),
                library.xdd_signal_width(database, index),
                _text(library.xdd_signal_file(database, index)),
                library.xdd_signal_line(database, index),
                library.xdd_signal_direction(database, index),
            ))
            signal_drivers = []
            for ordinal in range(library.xdd_trace_driver_count(database, index)):
                related = ctypes.c_int(-1); kind = ctypes.c_char_p()
                file_name = ctypes.c_char_p(); line = ctypes.c_int(0)
                library.xdd_trace_driver(database, index, ordinal, ctypes.byref(related),
                    ctypes.byref(kind), ctypes.byref(file_name), ctypes.byref(line))
                signal_drivers.append((related.value, _text(kind.value),
                    _text(library.xdd_trace_driver_role(database, index, ordinal)),
                    _text(library.xdd_trace_driver_predicate(database, index, ordinal)),
                    _text(file_name.value), line.value))
            drivers.append(signal_drivers)
            signal_loads = []
            for ordinal in range(library.xdd_trace_load_count(database, index)):
                related = ctypes.c_int(-1); kind = ctypes.c_char_p()
                file_name = ctypes.c_char_p(); line = ctypes.c_int(0)
                library.xdd_trace_load(database, index, ordinal, ctypes.byref(related),
                    ctypes.byref(kind), ctypes.byref(file_name), ctypes.byref(line))
                signal_loads.append((related.value, _text(kind.value),
                                     _text(file_name.value), line.value))
            loads.append(signal_loads)
            signal_ports = []
            for ordinal in range(library.xdd_port_connection_count(database, index)):
                related = ctypes.c_int(-1); kind = ctypes.c_char_p()
                library.xdd_port_connection(database, index, ordinal,
                    ctypes.byref(related), ctypes.byref(kind))
                signal_ports.append((related.value, _text(kind.value)))
            ports.append(signal_ports)
        name_index = sorted((signal[0], index)
                            for index, signal in enumerate(signals))
        resolve_results = [
            (name, library.xdd_resolve(database, name.encode("utf-8")))
            for name, _ in name_index
        ]
        resolve_results.append((
            "__xdebug_missing_signal__",
            library.xdd_resolve(database, b"__xdebug_missing_signal__"),
        ))
        return {"signals": signals, "name_index": name_index,
                "resolve_results": resolve_results, "drivers": drivers,
                "loads": loads, "ports": ports}
    finally:
        library.xdd_close(database)


def binary_snapshot(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    magic, major, minor, endian, header_size, file_size = HEADER_PREFIX.unpack_from(data)
    if (magic, major, minor, endian, header_size, file_size) != (
            MAGIC, 1, 0, 0x01020304, HEADER_SIZE, len(data)):
        raise ParityError("binary header mismatch")
    sections = [SECTION.unpack_from(data, HEADER_PREFIX.size + i * SECTION.size)
                for i in range(9)]
    strings_offset, strings_size = sections[8]

    def text(offset: int) -> str:
        if offset >= strings_size:
            raise ParityError("string offset outside pool")
        begin = strings_offset + offset
        end = data.find(b"\0", begin, strings_offset + strings_size)
        if end < 0:
            raise ParityError("unterminated string")
        return data[begin:end].decode("utf-8")

    signal_offset, signal_count = sections[0]
    signals = []
    for index in range(signal_count):
        name, kind, file_name, width, line, direction = SIGNAL.unpack_from(
            data, signal_offset + index * SIGNAL.size)
        signals.append((text(name), text(kind), width, text(file_name), line, direction))

    name_offset, name_count = sections[1]
    name_index = []
    for index in range(name_count):
        name, signal = NAME_INDEX.unpack_from(
            data, name_offset + index * NAME_INDEX.size)
        name_index.append((text(name), signal))

    def resolve(name: str) -> int:
        low, high = 0, len(name_index)
        while low < high:
            middle = low + (high - low) // 2
            candidate, signal = name_index[middle]
            if candidate == name:
                return signal
            if name < candidate:
                high = middle
            else:
                low = middle + 1
        return -1

    resolve_results = [(name, resolve(name)) for name, _ in name_index]
    resolve_results.append(("__xdebug_missing_signal__",
                            resolve("__xdebug_missing_signal__")))

    def starts(section_index: int) -> list[int]:
        offset, count = sections[section_index]
        return [START.unpack_from(data, offset + i * START.size)[0] for i in range(count)]

    driver_starts = starts(3); load_starts = starts(5); port_starts = starts(7)
    driver_offset, _ = sections[2]; load_offset, _ = sections[4]; port_offset, _ = sections[6]
    drivers = []
    loads = []
    ports = []
    for signal in range(signal_count):
        signal_drivers = []
        for ordinal in range(driver_starts[signal], driver_starts[signal + 1]):
            target, source, kind, role, predicate, file_name, line = DRIVER.unpack_from(
                data, driver_offset + ordinal * DRIVER.size)
            if target != signal:
                raise ParityError("driver grouping mismatch")
            signal_drivers.append((source, text(kind), text(role), text(predicate),
                                   text(file_name), line))
        drivers.append(signal_drivers)
        signal_loads = []
        for ordinal in range(load_starts[signal], load_starts[signal + 1]):
            source, consumer, kind, file_name, line = LOAD.unpack_from(
                data, load_offset + ordinal * LOAD.size)
            if source != signal:
                raise ParityError("load grouping mismatch")
            signal_loads.append((consumer, text(kind), text(file_name), line))
        loads.append(signal_loads)
        signal_ports = []
        for ordinal in range(port_starts[signal], port_starts[signal + 1]):
            owner, connected, kind = PORT.unpack_from(
                data, port_offset + ordinal * PORT.size)
            if owner != signal:
                raise ParityError("port grouping mismatch")
            signal_ports.append((connected, text(kind)))
        ports.append(signal_ports)
    return {"signals": signals, "name_index": name_index,
            "resolve_results": resolve_results, "drivers": drivers,
            "loads": loads, "ports": ports}


def compare(shared_library: Path, binary: Path) -> dict[str, Any]:
    expected = so_snapshot(shared_library)
    actual = binary_snapshot(binary)
    if actual != expected:
        for field in expected:
            if actual[field] != expected[field]:
                raise ParityError(f"binary differs from XDD SO in {field}")
        raise ParityError("binary differs from XDD SO")
    canonical = json.dumps(actual, separators=(",", ":"), ensure_ascii=False)
    return {
        "signals": len(actual["signals"]),
        "drivers": sum(map(len, actual["drivers"])),
        "loads": sum(map(len, actual["loads"])),
        "ports": sum(map(len, actual["ports"])),
        "canonical_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shared_library", type=Path)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    try:
        result = compare(args.shared_library, args.binary)
    except (OSError, UnicodeDecodeError, struct.error, ParityError) as error:
        print(f"parity failed: {error}")
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
