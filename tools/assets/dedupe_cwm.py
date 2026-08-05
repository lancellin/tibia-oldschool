#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path


def read_cwm_entries(path: Path) -> tuple[int, int, list[tuple[int, bytes]]]:
    data = path.read_bytes()
    if len(data) < 7:
        raise ValueError(f"{path} is too small to be a CWM file")

    version, sprites_count, entry_count = struct.unpack_from("<BHI", data, 0)
    position = 7
    metadata: list[tuple[int, int, int]] = []
    for _ in range(entry_count):
        offset, size, name_length = struct.unpack_from("<IIH", data, position)
        position += 10
        name = data[position:position + name_length].decode("utf-8")
        position += name_length
        metadata.append((int(name), offset, size))

    payload_start = position
    entries: list[tuple[int, bytes]] = []
    for sprite_id, offset, size in metadata:
        payload = data[payload_start + offset:payload_start + offset + size]
        if len(payload) != size:
            raise ValueError(f"{path} contains a truncated payload for sprite {sprite_id}")
        entries.append((sprite_id, payload))

    return version, sprites_count, entries


def write_deduped_cwm(path: Path, version: int, sprites_count: int, entries: list[tuple[int, bytes]]) -> dict[str, int]:
    ordered = sorted(entries)
    payload_offsets: dict[bytes, int] = {}
    unique_payloads: list[bytes] = []
    output_entries: list[tuple[int, int, int]] = []
    payload_bytes = 0

    for sprite_id, payload in ordered:
        digest = hashlib.sha256(payload).digest()
        if digest not in payload_offsets:
            payload_offsets[digest] = payload_bytes
            unique_payloads.append(payload)
            payload_bytes += len(payload)
        output_entries.append((sprite_id, payload_offsets[digest], len(payload)))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(struct.pack("<BHI", version, sprites_count, len(output_entries)))
        for sprite_id, offset, size in output_entries:
            name = str(sprite_id).encode("utf-8")
            output.write(struct.pack("<IIH", offset, size, len(name)))
            output.write(name)
        for payload in unique_payloads:
            output.write(payload)

    return {
        "entries": len(output_entries),
        "uniquePayloads": len(unique_payloads),
        "duplicateEntries": len(output_entries) - len(unique_payloads),
        "payloadBytes": payload_bytes,
    }


def dedupe_cwm(input_path: Path, output_path: Path, backup_path: Path | None = None) -> dict[str, object]:
    version, sprites_count, entries = read_cwm_entries(input_path)
    old_size = input_path.stat().st_size

    if backup_path:
        backup_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(input_path, backup_path)

    summary = write_deduped_cwm(output_path, version, sprites_count, entries)
    new_size = output_path.stat().st_size

    return {
        "input": str(input_path),
        "output": str(output_path),
        "backup": str(backup_path) if backup_path else None,
        "version": version,
        "spritesCount": sprites_count,
        "oldSize": old_size,
        "newSize": new_size,
        "bytesSaved": old_size - new_size,
        **summary,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Rewrite a Tibia CWM with shared offsets for identical PNG payloads.")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    summary = dedupe_cwm(args.input, args.output, args.backup)
    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
