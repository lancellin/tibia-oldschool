#!/usr/bin/env python3
import argparse
import json
import re
import shutil
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


def client_id_from_name(path: Path) -> int | None:
    match = re.match(r"item-(\d+)-object-context", path.name)
    if not match:
        return None
    return int(match.group(1))


def apply_original_alpha(tile: Image.Image, original_folder: Path, sprite_id: int, size: tuple[int, int]) -> Image.Image:
    result = tile.convert("RGBA")
    original_path = original_folder / f"{sprite_id}.png"
    if not original_path.exists():
        return result
    with Image.open(original_path) as original:
        alpha = original.convert("RGBA").getchannel("A").resize(size, Image.Resampling.LANCZOS)
    result.putalpha(alpha)
    return result


def extract_item(source: Path, original_root: Path, downscaled_dir: Path, sprites_dir: Path) -> dict[str, object]:
    client_id = client_id_from_name(source)
    if client_id is None:
        raise ValueError(f"Cannot infer client id from {source.name}")

    original_folder = original_root / f"item-{client_id}"
    metadata_path = original_folder / "metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    width = int(metadata["size"]["width"])
    height = int(metadata["size"]["height"])
    source_tile_w = 32
    source_tile_h = 32
    final_tile_w = 64
    final_tile_h = 64

    with Image.open(source) as image:
        upscaled = image.convert("RGBA")

    expected = (width * source_tile_w * 4, height * source_tile_h * 4)
    if upscaled.size != expected:
        raise ValueError(f"{source} expected {expected[0]}x{expected[1]}, got {upscaled.size}")

    downscaled = upscaled.resize((width * final_tile_w, height * final_tile_h), Image.Resampling.LANCZOS)
    downscaled_path = downscaled_dir / f"item-{client_id}-object-context.png"
    downscaled.save(downscaled_path)

    sprites = [int(sprite_id) for sprite_id in metadata["sprites"][: width * height]]
    extracted: list[int] = []
    for index, sprite_id in enumerate(sprites):
        if sprite_id <= 0:
            continue
        x = index % width
        y = index // width
        left = x * final_tile_w
        top = y * final_tile_h
        tile = downscaled.crop((left, top, left + final_tile_w, top + final_tile_h))
        tile = apply_original_alpha(tile, original_folder, sprite_id, (final_tile_w, final_tile_h))
        tile.save(sprites_dir / f"{sprite_id}.png")
        extracted.append(sprite_id)

    return {
        "clientId": client_id,
        "source": str(source),
        "downscaled": str(downscaled_path),
        "size": f"{width}x{height}",
        "sprites": extracted,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 2x CWM from 4x Upscayl object-context outputs.")
    parser.add_argument("--results-root", required=True, help="Folder containing 4x object-context PNGs")
    parser.add_argument("--original-root", required=True, help="Original extracted item root containing item-*/metadata.json")
    parser.add_argument("--out-root", required=True, help="Output root")
    parser.add_argument("--cwm-name", default="Tibia.object-context-4x-to-2x.cwm")
    parser.add_argument("--sprites-count", type=int, default=10962)
    args = parser.parse_args()

    results_root = Path(args.results_root)
    original_root = Path(args.original_root)
    out_root = Path(args.out_root)
    downscaled_dir = out_root / "processed-2x"
    sprites_dir = out_root / "sprites"
    built_dir = out_root / "built"

    if out_root.exists():
        shutil.rmtree(out_root)
    downscaled_dir.mkdir(parents=True, exist_ok=True)
    sprites_dir.mkdir(parents=True, exist_ok=True)
    built_dir.mkdir(parents=True, exist_ok=True)

    sources = sorted(results_root.glob("item-*-object-context.png"))
    if not sources:
        raise SystemExit(f"No object-context PNG files found in {results_root}")

    summary = [extract_item(source, original_root, downscaled_dir, sprites_dir) for source in sources]
    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)
    (out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "cwm": str(cwm_path),
        "itemCount": len(summary),
        "spriteCount": len(list(sprites_dir.glob("*.png"))),
        "summary": str(out_root / "summary.json"),
    }, indent=2))


if __name__ == "__main__":
    main()
