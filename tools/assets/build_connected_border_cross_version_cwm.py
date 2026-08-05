#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from build_cwm import build_cwm
from build_rme_ground_mosaic_cwm_from_upscayl import process_brush


SOURCE_SPRITE_BY_EDGE = {
    "n": 85,
    "e": 87,
    "s": 84,
    "w": 86,
    "cnw": 80,
    "cne": 81,
    "csw": 83,
    "cse": 82,
    "dnw": 79,
    "dne": 78,
    "dsw": 77,
    "dse": 76,
}


def unique_flatten(target_sprites: dict[str, list[int]]) -> list[int]:
    result: list[int] = []
    seen: set[int] = set()
    for sprite_ids in target_sprites.values():
        for sprite_id in sprite_ids:
            value = int(sprite_id)
            if value not in seen:
                result.append(value)
                seen.add(value)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 772 CWM from a connected 740 grass and border mosaic.")
    parser.add_argument("--brush-dir", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--base-target-manifest", type=Path, required=True)
    parser.add_argument("--border-target-manifest", type=Path, required=True)
    parser.add_argument("--cwm-name", required=True)
    parser.add_argument("--upscale-factor", type=int, default=2, choices=[2, 4])
    parser.add_argument("--image-name", required=True)
    parser.add_argument("--sprites-count", type=int, default=10962)
    args = parser.parse_args()

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    processed_dir = args.out_root / "processed-2x"
    source_sprites_dir = args.out_root / "source-sprites"
    sprites_dir = args.out_root / "sprites"
    built_dir = args.out_root / "built"
    processed_dir.mkdir(parents=True)
    source_sprites_dir.mkdir()
    sprites_dir.mkdir()
    built_dir.mkdir()

    extraction = process_brush(
        args.brush_dir,
        processed_dir,
        source_sprites_dir,
        args.upscale_factor,
        args.image_name,
    )
    if not extraction or extraction.get("status") != "processed":
        raise SystemExit(f"Connected mosaic was not processed: {extraction}")

    connected = json.loads((args.brush_dir / "_manifest.json").read_text(encoding="utf-8"))
    base_targets = json.loads(args.base_target_manifest.read_text(encoding="utf-8"))
    border_targets = json.loads(args.border_target_manifest.read_text(encoding="utf-8"))
    base_sprite_id = int(connected["baseSpriteId"])

    source_to_targets: dict[int, list[int]] = {
        base_sprite_id: unique_flatten(base_targets["targetSprites"]),
    }
    for entry in border_targets["tileVariants"]:
        edge = str(entry["edge"])
        source_id = SOURCE_SPRITE_BY_EDGE[edge]
        manifest_source_id = int(entry["clientId"])
        source_to_targets[source_id] = [
            int(target_id)
            for target_id in border_targets["targetSprites"].get(str(manifest_source_id), [])
        ]

    claimed_targets: dict[int, int] = {}
    written: list[dict[str, int]] = []
    for source_id, target_ids in source_to_targets.items():
        source_path = source_sprites_dir / f"{source_id}.png"
        if not source_path.exists():
            raise FileNotFoundError(f"Extracted source sprite is missing: {source_path}")
        for target_id in target_ids:
            previous_source = claimed_targets.get(target_id)
            if previous_source is not None and previous_source != source_id:
                raise ValueError(f"Target sprite {target_id} is mapped from both {previous_source} and {source_id}")
            shutil.copy2(source_path, sprites_dir / f"{target_id}.png")
            claimed_targets[target_id] = source_id
            written.append({"sourceSpriteId": source_id, "targetSpriteId": target_id})

    if not written:
        raise SystemExit("No mapped target sprites were written")

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)
    summary = {
        "source": str(args.brush_dir / args.image_name),
        "upscaleFactor": args.upscale_factor,
        "resized": args.upscale_factor != 2,
        "extraction": extraction,
        "sourceToTargets": {str(key): value for key, value in source_to_targets.items()},
        "writtenCount": len(written),
        "written": written,
        "cwm": str(cwm_path),
    }
    summary_path = args.out_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "cwm": str(cwm_path),
        "summary": str(summary_path),
        "sourceSprites": len(source_to_targets),
        "targetSprites": len(written),
        "resized": False if args.upscale_factor == 2 else True,
    }, indent=2))


if __name__ == "__main__":
    main()
