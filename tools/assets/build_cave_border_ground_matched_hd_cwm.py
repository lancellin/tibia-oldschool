#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm
from merge_cwm import merge_cwm, read_cwm


CORNER_OPPOSITE = {
    "cnw": "cse",
    "cne": "csw",
    "csw": "cne",
    "cse": "cnw",
}


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Recut the treated CID 351 cave-border mosaic into a merged 2x CWM."
    )
    parser.add_argument("--mosaic-dir", type=Path, required=True)
    parser.add_argument("--upscaled", type=Path, required=True)
    parser.add_argument("--classic-sprites", type=Path, required=True)
    parser.add_argument("--base-cwm", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads((args.mosaic_dir / "_manifest.json").read_text(encoding="utf-8"))
    if manifest.get("type") != "square-border-cutout-mosaic-v1":
        raise ValueError(f"Unsupported manifest type: {manifest.get('type')}")

    grid = int(manifest["grid"])
    tile_size = int(manifest["tileSize"])
    padding = int(manifest["padding"])
    final_tile_size = tile_size * 2
    expected_size = (
        (grid * tile_size + padding * 2) * 2,
        (grid * tile_size + padding * 2) * 2,
    )
    processed = Image.open(args.upscaled).convert("RGBA")
    if processed.size != expected_size:
        raise ValueError(f"Upscaled mosaic expected {expected_size}, got {processed.size}")

    if args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    sprites_dir = args.out_dir / "sprites"
    built_dir = args.out_dir / "built"
    sprites_dir.mkdir(parents=True)
    built_dir.mkdir()

    placements = manifest["placements"]
    edge_sprite_ids = {key: int(value) for key, value in manifest["edgeSpriteIds"].items()}
    occurrences: dict[int, list[dict[str, object]]] = {}
    for placement in placements:
        sprite_id = placement.get("borderSpriteId")
        if sprite_id is None:
            continue
        occurrences.setdefault(int(sprite_id), []).append(placement)

    center_x = (grid - 1) / 2
    center_y = (grid - 1) / 2
    selected = []
    for sprite_id, options in sorted(occurrences.items()):
        choice = min(
            options,
            key=lambda row: (
                (int(row["tileX"]) - center_x) ** 2
                + (int(row["tileY"]) - center_y) ** 2
            ),
        )
        left = (padding + int(choice["tileX"]) * tile_size) * 2
        top = (padding + int(choice["tileY"]) * tile_size) * 2
        tile = processed.crop(
            (left, top, left + final_tile_size, top + final_tile_size)
        ).convert("RGBA")

        classic = Image.open(args.classic_sprites / f"{sprite_id}.png").convert("RGBA")
        alpha = classic.getchannel("A").resize(
            (final_tile_size, final_tile_size),
            Image.Resampling.LANCZOS,
        )
        tile.putalpha(alpha)
        tile.save(sprites_dir / f"{sprite_id}.png")
        selected.append(
            {
                "spriteId": sprite_id,
                "mode": "direct-mosaic-crop",
                "tileX": int(choice["tileX"]),
                "tileY": int(choice["tileY"]),
            }
        )

    # The square uses small corners. Build the four large diagonal pieces from
    # the corresponding upscaled corner texture while preserving their alpha.
    diagonal_by_corner = {
        "cnw": 10105,
        "cne": 10106,
        "csw": 10107,
        "cse": 1185,
    }
    corner_position = {
        "cnw": (0, 0),
        "cne": (grid - 1, 0),
        "csw": (0, grid - 1),
        "cse": (grid - 1, grid - 1),
    }
    for corner, sprite_id in diagonal_by_corner.items():
        source_corner = CORNER_OPPOSITE[corner]
        tile_x, tile_y = corner_position[source_corner]
        left = (padding + tile_x * tile_size) * 2
        top = (padding + tile_y * tile_size) * 2
        texture = processed.crop(
            (left, top, left + final_tile_size, top + final_tile_size)
        ).convert("RGBA")

        classic = Image.open(args.classic_sprites / f"{sprite_id}.png").convert("RGBA")
        alpha = classic.getchannel("A").resize(
            (final_tile_size, final_tile_size),
            Image.Resampling.LANCZOS,
        )
        texture.putalpha(alpha)
        texture.save(sprites_dir / f"{sprite_id}.png")
        selected.append(
            {
                "spriteId": sprite_id,
                "mode": "derived-diagonal",
                "sourceCorner": source_corner,
                "tileX": tile_x,
                "tileY": tile_y,
            }
        )

    version, sprite_count, _ = read_cwm(args.base_cwm)
    partial_cwm = built_dir / "Tibia.cave-border-351-ground-matched-hd-partial.cwm"
    build_cwm(sprites_dir, partial_cwm, sprite_count)
    merged_cwm = built_dir / "Tibia.cwm"
    merge_summary = merge_cwm(args.base_cwm, partial_cwm, merged_cwm)

    summary = {
        "upscaled": str(args.upscaled),
        "manifest": str(args.mosaic_dir / "_manifest.json"),
        "cwmVersion": version,
        "spriteCountHeader": sprite_count,
        "partialCwm": str(partial_cwm),
        "mergedCwm": str(merged_cwm),
        "selected": sorted(selected, key=lambda row: int(row["spriteId"])),
        "merge": merge_summary,
    }
    summary_path = args.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
