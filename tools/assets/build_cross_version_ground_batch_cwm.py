#!/usr/bin/env python3
import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


def choose_center_placement(placements: list[dict[str, object]], client_id: int, sprite_id: int, grid_w: int, grid_h: int):
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


def find_input_image(batch_dir: Path, manifest: dict[str, object], image_suffix: str | None) -> Path:
    if image_suffix:
        candidates = sorted(batch_dir.glob(f"*{image_suffix}*.png"), key=lambda path: path.stat().st_mtime, reverse=True)
        if candidates:
            return candidates[0]

    mosaic = Path(str(manifest["mosaic"]))
    candidate = batch_dir / mosaic.name
    if candidate.exists():
        return candidate

    candidates = sorted(
        [path for path in batch_dir.glob("*.png") if "contact-sheet" not in path.name],
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if candidates:
        return candidates[0]
    raise FileNotFoundError(f"No input PNG found in {batch_dir}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 772 CWM from 740->772 ground batch mosaics already upscaled to 2x/4x.")
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--cwm-name", required=True)
    parser.add_argument("--upscale-factor", type=int, default=2, choices=[2, 4])
    parser.add_argument("--image-suffix", help="Optional filename fragment for choosing treated PNGs.")
    parser.add_argument("--brush", action="append", help="Only process folders with this name. Can be repeated.")
    parser.add_argument("--sprites-count", type=int, default=10962)
    args = parser.parse_args()

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    processed_dir = args.out_root / "processed-2x"
    sprites_dir = args.out_root / "sprites"
    built_dir = args.out_root / "built"
    processed_dir.mkdir(parents=True, exist_ok=True)
    sprites_dir.mkdir(parents=True, exist_ok=True)
    built_dir.mkdir(parents=True, exist_ok=True)

    source_tiles_by_client: dict[int, list[Image.Image]] = defaultdict(list)
    target_sprites_by_client: dict[int, list[int]] = {}
    processed = []

    wanted_brushes = set(args.brush or [])
    for batch_dir in sorted(path for path in args.input_root.iterdir() if path.is_dir() and (path / "_manifest.json").exists()):
        if wanted_brushes and batch_dir.name not in wanted_brushes:
            continue
        manifest_path = batch_dir / "_manifest.json"
        if not manifest_path.exists():
            continue
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        image_path = find_input_image(batch_dir, manifest, args.image_suffix)

        tile_size = int(manifest["tileSize"])
        final_tile_size = tile_size * 2
        grid_w = int(manifest["grid"]["width"])
        grid_h = int(manifest["grid"]["height"])
        expected = (grid_w * tile_size * args.upscale_factor, grid_h * tile_size * args.upscale_factor)
        final_size = (grid_w * final_tile_size, grid_h * final_tile_size)

        with Image.open(image_path) as image:
            upscaled = image.convert("RGBA")
        if upscaled.size != expected:
            raise ValueError(f"{image_path} expected {expected}, got {upscaled.size}")
        image_2x = upscaled if upscaled.size == final_size else upscaled.resize(final_size, Image.Resampling.LANCZOS)

        processed_batch_dir = processed_dir / batch_dir.name
        processed_batch_dir.mkdir(parents=True, exist_ok=True)
        processed_path = processed_batch_dir / f"{batch_dir.name}-mosaic-2x.png"
        image_2x.save(processed_path)

        for client_id_text, sprite_ids in manifest["targetSprites"].items():
            target_sprites_by_client.setdefault(int(client_id_text), [int(sprite_id) for sprite_id in sprite_ids])

        extracted_count = 0
        seen: set[tuple[int, int]] = set()
        for entry in manifest["tileVariants"]:
            client_id = int(entry["clientId"])
            sprite_id = int(entry["spriteId"])
            key = (client_id, sprite_id)
            if key in seen:
                continue
            seen.add(key)
            placement = choose_center_placement(manifest["placements"], client_id, sprite_id, grid_w, grid_h)
            if placement is None:
                continue
            left = int(placement["tileX"]) * final_tile_size
            top = int(placement["tileY"]) * final_tile_size
            tile = image_2x.crop((left, top, left + final_tile_size, top + final_tile_size)).convert("RGBA")
            source_tiles_by_client[client_id].append(tile)
            extracted_count += 1

        processed.append({
            "batch": batch_dir.name,
            "source": str(image_path),
            "processed": str(processed_path),
            "sourceTiles": extracted_count,
        })

    written = []
    fallback_source_tiles = [tile for client_tiles in source_tiles_by_client.values() for tile in client_tiles]
    for client_id in sorted(target_sprites_by_client):
        source_tiles = source_tiles_by_client.get(client_id) or fallback_source_tiles
        if not source_tiles:
            continue
        target_sprites = target_sprites_by_client.get(client_id, [])
        for index, target_sprite_id in enumerate(target_sprites):
            source_tiles[index % len(source_tiles)].save(sprites_dir / f"{target_sprite_id}.png")
            written.append({
                "clientId": client_id,
                "targetSpriteId": target_sprite_id,
                "sourceTileIndex": index % len(source_tiles),
            })

    if not written:
        raise SystemExit("No sprites written")

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)
    summary = {
        "inputRoot": str(args.input_root),
        "cwm": str(cwm_path),
        "processedBatchCount": len(processed),
        "writtenCount": len(written),
        "processed": processed,
        "written": written,
    }
    summary_path = args.out_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "cwm": str(cwm_path),
        "summary": str(summary_path),
        "processedBatchCount": len(processed),
        "sprites": len(written),
    }, indent=2))


if __name__ == "__main__":
    main()
