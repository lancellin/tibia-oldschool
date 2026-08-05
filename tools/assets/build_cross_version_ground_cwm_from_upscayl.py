#!/usr/bin/env python3
import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


DEFAULT_TARGET_DAT = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")
DEFAULT_TARGET_SPRITES_COUNT = 10962


def find_input_png(brush_dir: Path, image_name: str | None, upscale_factor: int) -> Path | None:
    if image_name:
        candidate = brush_dir / image_name
        return candidate if candidate.exists() else None

    factor_text = f"upscayl_{upscale_factor}x"
    candidates = sorted(
        [
            path
            for path in brush_dir.glob("*.png")
            if "upscayl" in path.name.lower() and factor_text in path.name.lower()
        ],
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


def choose_center_sprite_placement(
    placements: list[dict[str, object]],
    client_id: int,
    sprite_id: int,
    grid_w: int,
    grid_h: int,
) -> dict[str, object] | None:
    matches = [
        placement
        for placement in placements
        if int(placement.get("clientId", -1)) == client_id and int(placement.get("spriteId", -1)) == sprite_id
    ]
    if not matches:
        return None

    center_x = (grid_w - 1) / 2
    center_y = (grid_h - 1) / 2
    return min(
        matches,
        key=lambda placement: (float(placement["tileX"]) - center_x) ** 2 + (float(placement["tileY"]) - center_y) ** 2,
    )


def target_sprites_by_client(target_dat: Path, target_version: int, client_ids: list[int]) -> dict[int, list[int]]:
    things = parse_dat(target_dat, target_version, THING_CATEGORY_ITEM, set(client_ids))
    missing = [client_id for client_id in client_ids if client_id not in things]
    if missing:
        raise SystemExit(f"Target client ids not found in DAT: {', '.join(str(client_id) for client_id in missing)}")
    return {client_id: things[client_id].unique_sprites for client_id in client_ids}


def extract_source_tiles(
    manifest: dict[str, object],
    image_path: Path,
    upscale_factor: int,
    processed_dir: Path,
) -> tuple[dict[int, list[Image.Image]], Path]:
    source_tile_size = int(manifest["tileSize"])
    final_tile_size = source_tile_size * 2
    grid_w = int(manifest["grid"]["width"])
    grid_h = int(manifest["grid"]["height"])
    expected_size = (grid_w * source_tile_size * upscale_factor, grid_h * source_tile_size * upscale_factor)
    final_size = (grid_w * final_tile_size, grid_h * final_tile_size)

    with Image.open(image_path) as image:
        upscaled = image.convert("RGBA")
    if upscaled.size != expected_size:
        raise ValueError(f"{image_path} expected {expected_size}, got {upscaled.size}")

    image_2x = upscaled if upscaled.size == final_size else upscaled.resize(final_size, Image.Resampling.LANCZOS)
    processed_dir.mkdir(parents=True, exist_ok=True)
    downscaled_path = processed_dir / f"{manifest['brush']}-mosaic-2x.png"
    image_2x.save(downscaled_path)

    placements = manifest["placements"]
    source_tiles: dict[int, list[Image.Image]] = defaultdict(list)
    seen: set[tuple[int, int]] = set()
    for entry in manifest["tileVariants"]:
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
        key = (client_id, sprite_id)
        if key in seen:
            continue
        seen.add(key)

        placement = choose_center_sprite_placement(placements, client_id, sprite_id, grid_w, grid_h)
        if placement is None:
            continue

        left = int(placement["tileX"]) * final_tile_size
        top = int(placement["tileY"]) * final_tile_size
        tile = image_2x.crop((left, top, left + final_tile_size, top + final_tile_size)).convert("RGBA")
        source_tiles[client_id].append(tile)

    return source_tiles, downscaled_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 772 CWM from a treated mosaic generated from another DAT version.")
    parser.add_argument("--brush-dir", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, default=DEFAULT_TARGET_DAT)
    parser.add_argument("--target-version", type=int, default=772)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--cwm-name", required=True)
    parser.add_argument("--upscale-factor", type=int, default=4, choices=[2, 4])
    parser.add_argument("--image-name")
    parser.add_argument("--sprites-count", type=int, default=DEFAULT_TARGET_SPRITES_COUNT)
    args = parser.parse_args()

    manifest_path = args.brush_dir / "_manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"Missing manifest: {manifest_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image_path = find_input_png(args.brush_dir, args.image_name, args.upscale_factor)
    if image_path is None:
        raise SystemExit(f"No input PNG found in {args.brush_dir}")

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    processed_dir = args.out_root / "processed-2x"
    sprites_dir = args.out_root / "sprites"
    built_dir = args.out_root / "built"
    sprites_dir.mkdir(parents=True, exist_ok=True)
    built_dir.mkdir(parents=True, exist_ok=True)

    client_ids = [int(entry["clientId"]) for entry in manifest["entries"]]
    source_tiles, downscaled_path = extract_source_tiles(manifest, image_path, args.upscale_factor, processed_dir)
    target_sprites = target_sprites_by_client(args.target_dat, args.target_version, client_ids)

    written = []
    for client_id in client_ids:
        tiles = source_tiles.get(client_id, [])
        if not tiles:
            continue
        for index, target_sprite_id in enumerate(target_sprites[client_id]):
            tile = tiles[index % len(tiles)]
            tile.save(sprites_dir / f"{target_sprite_id}.png")
            written.append({
                "clientId": client_id,
                "sourceTileIndex": index % len(tiles),
                "targetSpriteId": target_sprite_id,
            })

    if not written:
        raise SystemExit("No sprites were written")

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)

    summary = {
        "sourceDatVersion": manifest.get("datVersion"),
        "targetDatVersion": args.target_version,
        "source": str(image_path),
        "downscaled": str(downscaled_path),
        "cwm": str(cwm_path),
        "writtenCount": len(written),
        "written": written,
    }
    summary_path = args.out_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({"cwm": str(cwm_path), "summary": str(summary_path), "sprites": len(written)}, indent=2))


if __name__ == "__main__":
    main()
