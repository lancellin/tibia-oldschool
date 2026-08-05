#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_mapping(spec: str) -> dict[int, int]:
    result = {}
    for pair in spec.split(","):
        client_id, source_sprite_id = pair.split(":", 1)
        result[int(client_id)] = int(source_sprite_id)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Replace existing target SPR slots by Client ID using classic SPR payloads.")
    parser.add_argument("--source-spr", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, required=True)
    parser.add_argument("--target-spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target-version", type=int, default=772)
    parser.add_argument("--mapping", required=True, help="ClientID:sourceSpriteID pairs, comma-separated")
    args = parser.parse_args()

    client_to_source = parse_mapping(args.mapping)
    things = parse_dat(
        args.target_dat,
        args.target_version,
        THING_CATEGORY_ITEM,
        set(client_to_source),
    )
    source_signature, source_count, source_sprites = read_spr(args.source_spr)
    target_signature, target_count, target_sprites = read_spr(args.target_spr)
    patched = dict(target_sprites)
    written = []
    claimed = {}

    for client_id, source_sprite_id in client_to_source.items():
        source_payload = source_sprites.get(source_sprite_id)
        if source_payload is None:
            raise ValueError(f"Source sprite {source_sprite_id} is missing")
        target_ids = [int(sprite_id) for sprite_id in things[client_id].unique_sprites if int(sprite_id) > 0]
        for target_id in target_ids:
            if target_id > target_count:
                raise ValueError(f"Target sprite {target_id} is outside 1-{target_count}")
            previous = claimed.get(target_id)
            if previous is not None and previous != source_sprite_id:
                raise ValueError(f"Target sprite {target_id} has conflicting sources")
            patched[target_id] = source_payload
            claimed[target_id] = source_sprite_id
            written.append({
                "clientId": client_id,
                "sourceSpriteId": source_sprite_id,
                "targetSpriteId": target_id,
                "payloadSha256": sha256(source_payload),
            })

    write_spr(args.output, target_signature, target_count, patched)
    summary = {
        "sourceSpr": str(args.source_spr),
        "sourceSignature": source_signature.hex(),
        "sourceSpriteCount": source_count,
        "targetSpr": str(args.target_spr),
        "targetSignature": target_signature.hex(),
        "targetSpriteCount": target_count,
        "output": str(args.output),
        "clientToSourceSprite": client_to_source,
        "patchedSlotCount": len(written),
        "written": written,
    }
    args.output.with_suffix(".summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "output": str(args.output),
        "targetSpriteCount": target_count,
        "patchedSlotCount": len(written),
        "clientToSourceSprite": client_to_source,
    }, indent=2))


if __name__ == "__main__":
    main()
