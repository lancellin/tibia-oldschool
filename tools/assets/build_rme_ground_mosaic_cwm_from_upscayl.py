#!/usr/bin/env python3
import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


DEFAULT_INPUT = Path(r"D:\AI\ComfyUI\input\rme-ground-brush-mosaics-1x")
DEFAULT_OUTPUT = Path(r"C:\tibia-oldschool\tools\assets\tests\rme-ground-mosaics-standard-4x-tta-to-2x")


def find_upscaled_png(brush_dir: Path, upscale_factor: int, image_name: str | None) -> Path | None:
    if image_name:
        candidate = brush_dir / image_name
        return candidate if candidate.exists() else None

    factor_text = f"upscayl_{upscale_factor}x"
    candidates = sorted(
        [
            path
            for path in brush_dir.glob("*.png")
            if "upscayl" in path.name.lower()
            and factor_text in path.name.lower()
        ],
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


def choose_center_placement(placements: list[dict[str, object]], client_id: int, grid_w: int, grid_h: int) -> dict[str, object] | None:
    matches = [placement for placement in placements if int(placement["clientId"]) == client_id]
    if not matches:
        return None
    center_x = (grid_w - 1) / 2
    center_y = (grid_h - 1) / 2
    return min(
        matches,
        key=lambda placement: (float(placement["tileX"]) - center_x) ** 2 + (float(placement["tileY"]) - center_y) ** 2,
    )


def choose_center_sprite_placement(placements: list[dict[str, object]], sprite_id: int, grid_w: int, grid_h: int) -> dict[str, object] | None:
    matches = [placement for placement in placements if int(placement.get("spriteId", -1)) == sprite_id]
    if not matches:
        return None
    center_x = (grid_w - 1) / 2
    center_y = (grid_h - 1) / 2
    return min(
        matches,
        key=lambda placement: (float(placement["tileX"]) - center_x) ** 2 + (float(placement["tileY"]) - center_y) ** 2,
    )


def choose_center_connected_placement(
    placements: list[dict[str, object]],
    sprite_id: int,
    base_sprite_id: int,
    grid_w: int,
    grid_h: int,
) -> dict[str, object] | None:
    if sprite_id == base_sprite_id:
        matches = [placement for placement in placements if placement.get("borderSpriteId") is None]
    else:
        matches = [
            placement
            for placement in placements
            if placement.get("borderSpriteId") is not None
            and int(placement["borderSpriteId"]) == sprite_id
        ]
    if not matches:
        return None
    center_x = (grid_w - 1) / 2
    center_y = (grid_h - 1) / 2
    return min(
        matches,
        key=lambda placement: (float(placement["tileX"]) - center_x) ** 2 + (float(placement["tileY"]) - center_y) ** 2,
    )


def apply_original_alpha(tile: Image.Image, source_tile: Path, size: tuple[int, int]) -> Image.Image:
    result = tile.convert("RGBA")
    if not source_tile.exists():
        return result
    with Image.open(source_tile) as original:
        alpha = original.convert("RGBA").getchannel("A").resize(size, Image.Resampling.LANCZOS)
    result.putalpha(alpha)
    return result


def process_brush(
    brush_dir: Path,
    processed_dir: Path,
    sprites_dir: Path,
    upscale_factor: int,
    image_name: str | None,
) -> dict[str, object] | None:
    manifest_path = brush_dir / "_manifest.json"
    if not manifest_path.exists():
        return None

    upscaled_path = find_upscaled_png(brush_dir, upscale_factor, image_name)
    if upscaled_path is None:
        return {
            "brush": brush_dir.name,
            "status": "skipped",
            "reason": f"no upscaled {upscale_factor}x PNG found",
            "folder": str(brush_dir),
        }

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    source_tile_size = int(manifest["tileSize"])
    final_tile_size = source_tile_size * 2
    grid_w = int(manifest["grid"]["width"])
    grid_h = int(manifest["grid"]["height"])
    expected_upscaled = (grid_w * source_tile_size * upscale_factor, grid_h * source_tile_size * upscale_factor)
    final_size = (grid_w * final_tile_size, grid_h * final_tile_size)

    with Image.open(upscaled_path) as image:
        upscaled = image.convert("RGBA")
    if upscaled.size != expected_upscaled:
        raise ValueError(f"{upscaled_path} expected {expected_upscaled}, got {upscaled.size}")

    downscaled = upscaled if upscaled.size == final_size else upscaled.resize(final_size, Image.Resampling.LANCZOS)
    processed_brush_dir = processed_dir / brush_dir.name
    processed_brush_dir.mkdir(parents=True, exist_ok=True)
    downscaled_path = processed_brush_dir / f"{brush_dir.name}-mosaic-2x.png"
    downscaled.save(downscaled_path)

    placements = manifest["placements"]
    extracted = []
    if manifest.get("type") == "connected-border-mosaic":
        base_sprite_id = int(manifest["baseSpriteId"])
        sprite_ids = [base_sprite_id, *[int(sprite_id) for sprite_id in manifest["borderSpriteIds"]]]
        for sprite_id in sprite_ids:
            placement = choose_center_connected_placement(
                placements,
                sprite_id,
                base_sprite_id,
                grid_w,
                grid_h,
            )
            if placement is None:
                continue
            left = int(placement["tileX"]) * final_tile_size
            top = int(placement["tileY"]) * final_tile_size
            tile = downscaled.crop((left, top, left + final_tile_size, top + final_tile_size))
            tile = apply_original_alpha(
                tile,
                brush_dir / "source-tiles" / f"{sprite_id}.png",
                (final_tile_size, final_tile_size),
            )
            tile.save(sprites_dir / f"{sprite_id}.png")
            extracted.append({"spriteId": sprite_id, "placement": placement})
    elif manifest.get("tileVariants"):
        seen_sprite_ids: set[int] = set()
        for entry in manifest["tileVariants"]:
            client_id = int(entry["clientId"])
            sprite_id = int(entry["spriteId"])
            if sprite_id in seen_sprite_ids:
                continue
            placement = choose_center_sprite_placement(placements, sprite_id, grid_w, grid_h)
            if placement is None:
                continue
            left = int(placement["tileX"]) * final_tile_size
            top = int(placement["tileY"]) * final_tile_size
            tile = downscaled.crop((left, top, left + final_tile_size, top + final_tile_size))
            tile = apply_original_alpha(
                tile,
                brush_dir / "source-tiles" / f"client-{client_id}-sprite-{sprite_id}.png",
                (final_tile_size, final_tile_size),
            )
            tile.save(sprites_dir / f"{sprite_id}.png")
            extracted.append({"clientId": client_id, "spriteId": sprite_id, "placement": placement})
            seen_sprite_ids.add(sprite_id)
    else:
        for entry in manifest["entries"]:
            client_id = int(entry["clientId"])
            placement = choose_center_placement(placements, client_id, grid_w, grid_h)
            if placement is None:
                continue
            left = int(placement["tileX"]) * final_tile_size
            top = int(placement["tileY"]) * final_tile_size
            tile = downscaled.crop((left, top, left + final_tile_size, top + final_tile_size))
            tile = apply_original_alpha(
                tile,
                brush_dir / "source-tiles" / f"client-{client_id}.png",
                (final_tile_size, final_tile_size),
            )

            metadata = manifest["itemMetadata"].get(str(client_id), {})
            sprite_ids = [int(sprite_id) for sprite_id in metadata.get("sprites", [])]
            if not sprite_ids:
                sprite_ids = [client_id]
            for sprite_id in sprite_ids:
                tile.save(sprites_dir / f"{sprite_id}.png")
                extracted.append({"clientId": client_id, "spriteId": sprite_id, "placement": placement})

    return {
        "brush": manifest.get("brush", brush_dir.name),
        "status": "processed",
        "source": str(upscaled_path),
        "downscaled": str(downscaled_path),
        "extractedCount": len(extracted),
        "extracted": extracted,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build 2x CWM from RME ground brush mosaic Upscayl outputs.")
    parser.add_argument("--input-root", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--cwm-name", default="Tibia.rme-ground-mosaics-standard-4x-tta-to-2x.cwm")
    parser.add_argument("--sprites-count", type=int, default=10962)
    parser.add_argument("--upscale-factor", type=int, default=4, choices=[2, 4], help="Input Upscayl scale. Use 2 to recut without downscale.")
    parser.add_argument("--image-name", help="Explicit input PNG name inside each brush folder.")
    args = parser.parse_args()

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    processed_dir = args.out_root / "processed-2x"
    sprites_dir = args.out_root / "sprites"
    built_dir = args.out_root / "built"
    processed_dir.mkdir(parents=True, exist_ok=True)
    sprites_dir.mkdir(parents=True, exist_ok=True)
    built_dir.mkdir(parents=True, exist_ok=True)

    summaries = []
    for brush_dir in sorted(path for path in args.input_root.iterdir() if path.is_dir()):
        summaries.append(process_brush(brush_dir, processed_dir, sprites_dir, args.upscale_factor, args.image_name))
    summaries = [summary for summary in summaries if summary is not None]

    if not any(summary.get("status") == "processed" for summary in summaries):
        raise SystemExit(f"No upscaled brush mosaics were processed from {args.input_root}")

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)
    summary_path = args.out_root / "summary.json"
    summary_path.write_text(json.dumps(summaries, indent=2), encoding="utf-8")

    by_status = defaultdict(int)
    for summary in summaries:
        by_status[summary.get("status", "unknown")] += 1

    print(json.dumps({
        "cwm": str(cwm_path),
        "summary": str(summary_path),
        "sprites": len(list(sprites_dir.glob("*.png"))),
        "statuses": dict(by_status),
    }, indent=2))


if __name__ == "__main__":
    main()
