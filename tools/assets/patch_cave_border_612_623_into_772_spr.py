#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr


# Mapped by alpha-mask orientation. These slots are also shared by CIDs 635-646.
CLIENT_TO_SOURCE = {
    4785: 612,
    4786: 615,
    4787: 613,
    4788: 614,
    4789: 620,
    4790: 621,
    4791: 623,
    4792: 622,
    4793: 616,
    4794: 617,
    4795: 619,
    4796: 618,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Replace the existing 772 cave-border slots with 7.4 sprites 612-623."
    )
    parser.add_argument("--source-spr", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, required=True)
    parser.add_argument("--target-spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source_signature, source_count, source_sprites = read_spr(args.source_spr)
    target_signature, target_count, target_sprites = read_spr(args.target_spr)
    all_things = parse_dat(
        args.target_dat,
        version=772,
        category=THING_CATEGORY_ITEM,
        target_ids=None,
    )

    missing_clients = sorted(set(CLIENT_TO_SOURCE) - set(all_things))
    if missing_clients:
        raise ValueError(f"Missing client IDs in target DAT: {missing_clients}")

    patched = dict(target_sprites)
    claimed_targets: dict[int, int] = {}
    groups = []
    for client_id, source_id in CLIENT_TO_SOURCE.items():
        source_block = source_sprites.get(source_id)
        if source_block is None:
            raise ValueError(f"Source sprite {source_id} is missing from {args.source_spr}")

        target_ids = all_things[client_id].unique_sprites
        if len(target_ids) != 1:
            raise ValueError(f"Client ID {client_id} should reference exactly one sprite slot")

        target_id = target_ids[0]
        if target_id > target_count:
            raise ValueError(f"Target sprite {target_id} is outside 1-{target_count}")
        previous = claimed_targets.get(target_id)
        if previous is not None and previous != source_id:
            raise ValueError(
                f"Target sprite {target_id} is mapped from both {previous} and {source_id}"
            )

        affected_clients = sorted(
            cid for cid, thing in all_things.items() if target_id in thing.unique_sprites
        )
        patched[target_id] = source_block
        claimed_targets[target_id] = source_id
        groups.append(
            {
                "clientId": client_id,
                "sourceSpriteId": source_id,
                "sourceBlockSha256": sha256(source_block),
                "targetSpriteId": target_id,
                "affectedClientIds": affected_clients,
            }
        )

    write_spr(args.output, target_signature, target_count, patched)

    output_signature, output_count, output_sprites = read_spr(args.output)
    if output_signature != target_signature or output_count != target_count:
        raise ValueError("Output SPR signature or sprite count changed")

    changed_ids = sorted(
        sprite_id
        for sprite_id in range(1, target_count + 1)
        if target_sprites.get(sprite_id) != output_sprites.get(sprite_id)
    )
    expected_ids = sorted(claimed_targets)
    if changed_ids != expected_ids:
        raise ValueError(
            f"Changed slots differ from expected: changed={changed_ids}, expected={expected_ids}"
        )

    for target_id, source_id in claimed_targets.items():
        if output_sprites.get(target_id) != source_sprites[source_id]:
            raise ValueError(f"Target sprite {target_id} does not match source {source_id}")

    summary = {
        "sourceSpr": str(args.source_spr),
        "sourceSignature": source_signature.hex(),
        "sourceSpriteCount": source_count,
        "targetDat": str(args.target_dat),
        "targetSpr": str(args.target_spr),
        "targetSignature": target_signature.hex(),
        "targetSpriteCount": target_count,
        "output": str(args.output),
        "changedSpriteCount": len(changed_ids),
        "changedSpriteIds": changed_ids,
        "groups": groups,
    }
    summary_path = args.output.with_suffix(".summary.json")
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
