#!/usr/bin/env python3
import argparse
import json
import struct
from pathlib import Path

from build_connected_border_cross_version_cwm import SOURCE_SPRITE_BY_EDGE, unique_flatten


def read_spr(path: Path) -> tuple[bytes, int, dict[int, bytes]]:
    data = path.read_bytes()
    if len(data) < 6:
        raise ValueError(f"{path} is too small to be a Tibia.spr file")

    signature = data[:4]
    sprite_count = struct.unpack_from("<H", data, 4)[0]
    table_end = 6 + sprite_count * 4
    if table_end > len(data):
        raise ValueError(f"{path} has a truncated sprite address table")

    sprites: dict[int, bytes] = {}
    for sprite_id in range(1, sprite_count + 1):
        address = struct.unpack_from("<I", data, 6 + (sprite_id - 1) * 4)[0]
        if address == 0:
            continue
        if address + 5 > len(data):
            raise ValueError(f"{path} sprite {sprite_id} points outside the file")
        payload_size = struct.unpack_from("<H", data, address + 3)[0]
        block_end = address + 5 + payload_size
        if block_end > len(data):
            raise ValueError(f"{path} sprite {sprite_id} has a truncated payload")
        sprites[sprite_id] = data[address:block_end]

    return signature, sprite_count, sprites


def write_spr(path: Path, signature: bytes, sprite_count: int, sprites: dict[int, bytes]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header_size = 6 + sprite_count * 4
    output = bytearray(header_size)
    output[:4] = signature
    struct.pack_into("<H", output, 4, sprite_count)

    for sprite_id in range(1, sprite_count + 1):
        block = sprites.get(sprite_id)
        if block is None:
            continue
        address = len(output)
        struct.pack_into("<I", output, 6 + (sprite_id - 1) * 4, address)
        output.extend(block)

    path.write_bytes(output)


def build_source_to_targets(
    base_sprite_id: int,
    base_target_manifest: dict[str, object],
    border_target_manifest: dict[str, object],
) -> dict[int, list[int]]:
    source_to_targets = {
        base_sprite_id: unique_flatten(base_target_manifest["targetSprites"]),
    }
    for entry in border_target_manifest["tileVariants"]:
        edge = str(entry["edge"])
        source_id = SOURCE_SPRITE_BY_EDGE[edge]
        manifest_source_id = int(entry["clientId"])
        source_to_targets[source_id] = [
            int(target_id)
            for target_id in border_target_manifest["targetSprites"].get(str(manifest_source_id), [])
        ]
    return source_to_targets


def main() -> None:
    parser = argparse.ArgumentParser(description="Patch classic 772 SPR slots with original 740 sprite payloads.")
    parser.add_argument("--source-spr", type=Path, required=True)
    parser.add_argument("--target-spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base-sprite-id", type=int, default=43)
    parser.add_argument("--base-target-manifest", type=Path, required=True)
    parser.add_argument("--border-target-manifest", type=Path, required=True)
    args = parser.parse_args()

    source_signature, source_count, source_sprites = read_spr(args.source_spr)
    target_signature, target_count, target_sprites = read_spr(args.target_spr)
    base_targets = json.loads(args.base_target_manifest.read_text(encoding="utf-8"))
    border_targets = json.loads(args.border_target_manifest.read_text(encoding="utf-8"))
    source_to_targets = build_source_to_targets(args.base_sprite_id, base_targets, border_targets)

    patched = dict(target_sprites)
    written = []
    claimed_targets: dict[int, int] = {}
    for source_id, target_ids in source_to_targets.items():
        if source_id not in source_sprites:
            raise ValueError(f"Source sprite {source_id} is missing from {args.source_spr}")
        for target_id in target_ids:
            if target_id <= 0 or target_id > target_count:
                raise ValueError(f"Target sprite {target_id} is outside 1-{target_count}")
            previous = claimed_targets.get(target_id)
            if previous is not None and previous != source_id:
                raise ValueError(f"Target sprite {target_id} is mapped from both {previous} and {source_id}")
            patched[target_id] = source_sprites[source_id]
            claimed_targets[target_id] = source_id
            written.append({"sourceSpriteId": source_id, "targetSpriteId": target_id})

    write_spr(args.output, target_signature, target_count, patched)
    summary = {
        "sourceSpr": str(args.source_spr),
        "sourceSignature": source_signature.hex(),
        "sourceSpriteCount": source_count,
        "targetSpr": str(args.target_spr),
        "targetSignature": target_signature.hex(),
        "targetSpriteCount": target_count,
        "output": str(args.output),
        "patchedCount": len(written),
        "sourceToTargets": {str(key): value for key, value in source_to_targets.items()},
        "written": written,
    }
    summary_path = args.output.with_suffix(".summary.json")
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "output": str(args.output),
        "summary": str(summary_path),
        "patchedSprites": len(written),
        "targetSpriteCount": target_count,
    }, indent=2))


if __name__ == "__main__":
    main()
