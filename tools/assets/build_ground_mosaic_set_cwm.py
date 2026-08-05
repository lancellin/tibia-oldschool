#!/usr/bin/env python3
import argparse
import json
import re
import shutil
from collections import defaultdict
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


LABEL_RE = re.compile(r"f(\d+)-z(\d+)-x(\d+)-y(\d+)")


def find_sprite_png(root: Path, sprite_id: int) -> Path:
    bucket_start = ((sprite_id - 1) // 1000) * 1000 + 1
    bucket_end = min(bucket_start + 999, 16114)
    path = root / f"{bucket_start:04d}-{bucket_end}" / f"{sprite_id}.png"
    if not path.exists():
        raise FileNotFoundError(f"Original sprite {sprite_id} not found at {path}")
    return path


def find_upscaled(folder: Path, expected_name: str, expected_size: tuple[int, int]) -> Path:
    source_stem = Path(expected_name).stem
    candidates: list[Path] = []
    for path in folder.rglob("*.png"):
        if path.name == expected_name and path.parent == folder:
            continue
        if not path.stem.startswith(source_stem):
            continue
        with Image.open(path) as image:
            if image.size == expected_size:
                candidates.append(path)
    if not candidates:
        raise FileNotFoundError(f"No {expected_size[0]}x{expected_size[1]} upscale found for {expected_name}")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def output_key(frame: int, z: int, frames: int, pattern_z: int) -> tuple[int, int]:
    return (frame if frames > 1 else 0, z if pattern_z > 1 else 0)


def apply_original_alpha(tile: Image.Image, original_path: Path) -> Image.Image:
    result = tile.convert("RGBA")
    with Image.open(original_path) as original:
        alpha = original.convert("RGBA").getchannel("A").resize(result.size, Image.Resampling.LANCZOS)
    result.putalpha(alpha)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Recut a ground mosaic set and build a partial 2x CWM.")
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--all-sprites", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--sprites-count", type=int, required=True)
    parser.add_argument("--cwm-name", default="Tibia.ground-mosaic-set-2x.cwm")
    args = parser.parse_args()

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    sprites_dir = args.out_root / "sprites-64"
    previews_dir = args.out_root / "recut-previews"
    built_dir = args.out_root / "built"
    sprites_dir.mkdir(parents=True)
    previews_dir.mkdir(parents=True)
    built_dir.mkdir(parents=True)

    candidates: dict[int, list[dict[str, object]]] = defaultdict(list)
    item_summaries: list[dict[str, object]] = []

    for folder in sorted(path for path in args.input_root.iterdir() if path.is_dir()):
        manifest_path = folder / "manifest.json"
        if not manifest_path.exists():
            continue
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        grid_w, grid_h = (int(value) for value in manifest["grid"])
        tile_w, tile_h = (int(value) for value in manifest["tileSize"])
        final_tile = (tile_w * 2, tile_h * 2)
        expected_size = (grid_w * final_tile[0], grid_h * final_tile[1])
        layout = manifest["layout"]
        frames = int(layout["frames"])
        pattern_z = int(layout["patternZ"])

        upscaled_by_key: dict[tuple[int, int], Path] = {}
        for output_value in manifest["outputs"]:
            output_name = Path(output_value).name
            frame_match = re.search(r"-frame-(\d+)", output_name)
            z_match = re.search(r"-z-(\d+)", output_name)
            frame = int(frame_match.group(1)) if frame_match else 0
            z = int(z_match.group(1)) if z_match else 0
            upscaled_by_key[output_key(frame, z, frames, pattern_z)] = find_upscaled(
                folder, output_name, expected_size
            )

        images = {
            key: Image.open(path).convert("RGBA")
            for key, path in upscaled_by_key.items()
        }
        extracted_for_item: set[int] = set()
        for label, refs_value in manifest["variantReferences"].items():
            match = LABEL_RE.fullmatch(label)
            if not match:
                raise ValueError(f"Unexpected variant label {label}")
            frame, z, px, py = (int(value) for value in match.groups())
            image = images[output_key(frame, z, frames, pattern_z)]

            matching_x = [x for x in range(grid_w) if x % int(layout["patternX"]) == px]
            matching_y = [y for y in range(grid_h) if y % int(layout["patternY"]) == py]
            center_x = (grid_w - 1) / 2
            center_y = (grid_h - 1) / 2
            tile_x = min(matching_x, key=lambda value: abs(value - center_x))
            tile_y = min(matching_y, key=lambda value: abs(value - center_y))
            left = tile_x * final_tile[0]
            top = tile_y * final_tile[1]
            crop = image.crop((left, top, left + final_tile[0], top + final_tile[1]))
            distance = (tile_x - center_x) ** 2 + (tile_y - center_y) ** 2

            for layer, sprite_value in enumerate(refs_value):
                sprite_id = int(sprite_value)
                if sprite_id <= 0:
                    continue
                original_path = find_sprite_png(args.all_sprites, sprite_id)
                sprite = apply_original_alpha(crop, original_path)
                candidates[sprite_id].append({
                    "image": sprite,
                    "itemFolder": folder.name,
                    "label": label,
                    "layer": layer,
                    "upperLayers": sum(1 for value in refs_value[layer + 1:] if int(value) > 0),
                    "distance": distance,
                })
                extracted_for_item.add(sprite_id)

        for image in images.values():
            image.close()
        item_summaries.append({
            "folder": folder.name,
            "clientId": manifest["clientId"],
            "upscales": {f"{key[0]}:{key[1]}": str(path) for key, path in upscaled_by_key.items()},
            "spriteIds": sorted(extracted_for_item),
        })

    conflicts: list[dict[str, object]] = []
    selected_rows: list[dict[str, object]] = []
    for sprite_id, options in sorted(candidates.items()):
        selected = min(
            options,
            key=lambda row: (
                int(row["upperLayers"]),
                float(row["distance"]),
                int(row["layer"]),
            ),
        )
        image = selected.pop("image")
        image.save(sprites_dir / f"{sprite_id}.png")
        selected_rows.append({"spriteId": sprite_id, **selected})
        if len(options) > 1:
            conflicts.append({
                "spriteId": sprite_id,
                "occurrences": len(options),
                "selected": selected,
                "sources": [
                    {key: value for key, value in option.items() if key != "image"}
                    for option in options
                ],
            })

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)
    summary = {
        "inputRoot": str(args.input_root),
        "items": item_summaries,
        "spriteCount": len(selected_rows),
        "selected": selected_rows,
        "conflicts": conflicts,
        "cwm": str(cwm_path),
    }
    (args.out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "items": len(item_summaries),
        "sprites": len(selected_rows),
        "conflicts": len(conflicts),
        "cwm": str(cwm_path),
    }, indent=2))


if __name__ == "__main__":
    main()
