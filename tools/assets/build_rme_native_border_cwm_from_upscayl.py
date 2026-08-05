#!/usr/bin/env python3
import argparse
import json
import shutil
import struct
from collections import defaultdict
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


DEFAULT_INPUT = Path(r"D:\tibia-oldschool\tools\assets\work\rme-native-border-mosaics")
DEFAULT_OUTPUT = Path(r"D:\tibia-oldschool\tools\assets\work\rme-native-border-cwm")
DEFAULT_SPR = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")


def find_processed_png(folder: Path, image_name: str | None, upscale_factor: int) -> Path | None:
    if image_name:
        candidate = folder / image_name
        return candidate if candidate.exists() else None
    factor_text = f"upscayl_{upscale_factor}x"
    candidates = [
        path
        for path in folder.glob("*.png")
        if "upscayl" in path.name.lower() and factor_text in path.name.lower()
    ]
    return max(candidates, key=lambda path: path.stat().st_mtime) if candidates else None


def original_alpha(source_dir: Path, sprite_id: int, size: tuple[int, int]) -> Image.Image:
    path = source_dir / f"{sprite_id}.png"
    if not path.exists():
        raise FileNotFoundError(f"Missing original sprite alpha source: {path}")
    with Image.open(path) as image:
        return image.convert("RGBA").getchannel("A").resize(size, Image.Resampling.LANCZOS)


def collect_occurrences(manifest: dict[str, object]) -> dict[int, list[dict[str, object]]]:
    result: dict[int, list[dict[str, object]]] = defaultdict(list)
    grid_w = int(manifest["grid"]["width"])
    grid_h = int(manifest["grid"]["height"])
    center_x = (grid_w - 1) / 2
    center_y = (grid_h - 1) / 2

    for placement in manifest["placements"]:
        tile_x = int(placement["tileX"])
        tile_y = int(placement["tileY"])
        distance = (tile_x - center_x) ** 2 + (tile_y - center_y) ** 2
        border_sprite_ids = [
            int(sprite_id)
            for item in placement["borderItems"]
            for sprite_id in item["spriteIds"]
        ]
        for sprite_id in placement["groundSpriteIds"]:
            result[int(sprite_id)].append(
                {
                    "role": "ground",
                    "tileX": tile_x,
                    "tileY": tile_y,
                    "contamination": len(border_sprite_ids),
                    "distance": distance,
                }
            )
        for sprite_id in border_sprite_ids:
            result[int(sprite_id)].append(
                {
                    "role": "border",
                    "tileX": tile_x,
                    "tileY": tile_y,
                    "contamination": max(0, len(border_sprite_ids) - 1),
                    "distance": distance,
                }
            )
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Recut a processed RME-native border mosaic and build a partial 2x CWM."
    )
    parser.add_argument("--brush-dir", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--image-name")
    parser.add_argument("--upscale-factor", type=int, choices=[2, 4], default=4)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--sprites-count", type=int)
    parser.add_argument("--cwm-name", default="Tibia.rme-native-border-test.cwm")
    args = parser.parse_args()

    manifest_path = args.brush_dir / "_manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"Missing manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("type") != "rme-native-border-mosaic-v1":
        raise SystemExit(f"Unsupported manifest type: {manifest.get('type')}")

    source = find_processed_png(args.brush_dir, args.image_name, args.upscale_factor)
    if source is None:
        raise SystemExit(f"No processed {args.upscale_factor}x PNG found in {args.brush_dir}")

    grid_w = int(manifest["grid"]["width"])
    grid_h = int(manifest["grid"]["height"])
    source_tile_size = int(manifest["tileSize"])
    expected = (
        grid_w * source_tile_size * args.upscale_factor,
        grid_h * source_tile_size * args.upscale_factor,
    )
    final_tile_size = source_tile_size * 2
    final_size = (grid_w * final_tile_size, grid_h * final_tile_size)
    with Image.open(source) as image:
        processed = image.convert("RGBA")
    if processed.size != expected:
        raise ValueError(f"{source} expected {expected}, got {processed.size}")
    image_2x = processed if processed.size == final_size else processed.resize(final_size, Image.Resampling.LANCZOS)

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    processed_dir = args.out_root / "processed-2x"
    sprites_dir = args.out_root / "sprites"
    built_dir = args.out_root / "built"
    processed_dir.mkdir(parents=True)
    sprites_dir.mkdir()
    built_dir.mkdir()
    downscaled_path = processed_dir / f"{args.brush_dir.name}-mosaic-2x.png"
    image_2x.save(downscaled_path)

    source_dir = Path(manifest["sourceTiles"])
    if not source_dir.is_absolute():
        source_dir = source_dir.resolve()
    occurrences = collect_occurrences(manifest)
    selected = []
    for sprite_id, options in sorted(occurrences.items()):
        choice = min(
            options,
            key=lambda row: (
                int(row["contamination"]),
                float(row["distance"]),
                0 if row["role"] == "ground" else 1,
            ),
        )
        left = int(choice["tileX"]) * final_tile_size
        top = int(choice["tileY"]) * final_tile_size
        tile = image_2x.crop((left, top, left + final_tile_size, top + final_tile_size)).convert("RGBA")
        tile.putalpha(original_alpha(source_dir, sprite_id, (final_tile_size, final_tile_size)))
        tile.save(sprites_dir / f"{sprite_id}.png")
        selected.append({"spriteId": sprite_id, **choice, "occurrences": len(options)})

    if not selected:
        raise SystemExit("No sprite occurrences were found in the mosaic manifest")

    sprites_count = args.sprites_count
    if sprites_count is None:
        spr_data = args.spr.read_bytes()
        sprites_count = struct.unpack_from("<H", spr_data, 4)[0]

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, sprites_count)
    summary = {
        "source": str(source),
        "manifest": str(manifest_path),
        "downscaled": str(downscaled_path),
        "spritesCountHeader": sprites_count,
        "spriteCount": len(selected),
        "selected": selected,
        "cwm": str(cwm_path),
        "warnings": manifest.get("warnings", []),
    }
    summary_path = args.out_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({"cwm": str(cwm_path), "summary": str(summary_path), "sprites": len(selected)}, indent=2))


if __name__ == "__main__":
    main()
