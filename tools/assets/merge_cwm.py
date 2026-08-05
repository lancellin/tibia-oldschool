#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def read_cwm(path: Path) -> tuple[int, int, dict[int, bytes]]:
    data = path.read_bytes()
    if len(data) < 7:
        raise ValueError(f"{path} is too small to be a CWM file")

    version, sprites_count, entry_count = struct.unpack_from("<BHI", data, 0)
    position = 7
    entries: list[tuple[int, int, int]] = []
    for _ in range(entry_count):
        offset, size, name_length = struct.unpack_from("<IIH", data, position)
        position += 10
        name = data[position:position + name_length].decode("utf-8")
        position += name_length
        entries.append((int(name), offset, size))

    payload_start = position
    sprites = {
        sprite_id: data[payload_start + offset:payload_start + offset + size]
        for sprite_id, offset, size in entries
    }
    if any(len(sprites[sprite_id]) != size for sprite_id, _, size in entries):
        raise ValueError(f"{path} contains a truncated sprite payload")
    return version, sprites_count, sprites


def write_cwm(path: Path, version: int, sprites_count: int, sprites: dict[int, bytes]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ordered = sorted(sprites.items())
    with path.open("wb") as output:
        output.write(struct.pack("<BHI", version, sprites_count, len(ordered)))

        offset = 0
        for sprite_id, payload in ordered:
            name = str(sprite_id).encode("utf-8")
            output.write(struct.pack("<IIH", offset, len(payload), len(name)))
            output.write(name)
            offset += len(payload)

        for _, payload in ordered:
            output.write(payload)


def merge_cwm(base_path: Path, overlay_path: Path, output_path: Path) -> dict[str, object]:
    base_version, base_count, base_sprites = read_cwm(base_path)
    overlay_version, overlay_count, overlay_sprites = read_cwm(overlay_path)
    if base_version != overlay_version:
        raise ValueError(f"CWM version mismatch: {base_version} != {overlay_version}")

    replaced = sorted(set(base_sprites) & set(overlay_sprites))
    added = sorted(set(overlay_sprites) - set(base_sprites))
    merged = {**base_sprites, **overlay_sprites}
    write_cwm(output_path, base_version, max(base_count, overlay_count), merged)
    return {
        "baseEntries": len(base_sprites),
        "overlayEntries": len(overlay_sprites),
        "mergedEntries": len(merged),
        "replacedIds": replaced,
        "addedIds": added,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Merge two partial Tibia CWM files, with overlay entries taking precedence.")
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--overlay", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    summary = merge_cwm(args.base, args.overlay, args.output)
    print(f"[ok] wrote {args.output}")
    print(
        f"[info] base={summary['baseEntries']} overlay={summary['overlayEntries']} "
        f"merged={summary['mergedEntries']} replaced={len(summary['replacedIds'])} "
        f"added={len(summary['addedIds'])}"
    )


if __name__ == "__main__":
    main()
