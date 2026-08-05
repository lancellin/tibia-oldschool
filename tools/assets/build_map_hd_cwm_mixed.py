#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def clean_numeric_output(out_dir: Path) -> None:
    if not out_dir.exists():
        return
    for png in out_dir.glob("*.png"):
        if png.stem.isdigit():
            png.unlink()


def read_image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def get_source_tile_size(original_folder: Path, sprite_ids: list[int]) -> tuple[int, int]:
    for sprite_id in sprite_ids:
        sprite_path = original_folder / f"{sprite_id}.png"
        if sprite_path.exists():
            return read_image_size(sprite_path)
    raise FileNotFoundError(f"No source sprite PNG found in {original_folder}")


def apply_original_alpha(tile: Image.Image, original_folder: Path, sprite_id: int) -> Image.Image:
    original_path = original_folder / f"{sprite_id}.png"
    if not original_path.exists():
        return tile.convert("RGBA")

    result = tile.convert("RGBA")
    with Image.open(original_path) as image:
        alpha = image.convert("RGBA").getchannel("A")

    if alpha.size != result.size:
        alpha = alpha.resize(result.size, Image.Resampling.LANCZOS)
    result.putalpha(alpha)
    return result


def extract_mosaic(
    processed_mosaic: Path,
    original_mosaic: Path,
    original_folder: Path,
    metadata: dict[str, object],
    out_dir: Path,
) -> int:
    sprite_ids = [int(sprite_id) for sprite_id in metadata["uniqueSprites"]]
    patterns = metadata["patterns"]
    pattern_w = int(patterns["x"])
    pattern_h = int(patterns["y"])
    pattern_z = int(patterns["z"])

    if pattern_z != 1 or len(sprite_ids) != pattern_w * pattern_h:
        raise ValueError(f"Cannot extract mosaic for item {metadata['clientId']}: unsupported pattern shape")

    src_tile_w, src_tile_h = get_source_tile_size(original_folder, sprite_ids)
    original_w, original_h = read_image_size(original_mosaic)
    grid_w = original_w // src_tile_w
    grid_h = original_h // src_tile_h

    with Image.open(processed_mosaic) as image:
        processed = image.convert("RGBA")

    if processed.width % grid_w != 0 or processed.height % grid_h != 0:
        raise ValueError(
            f"{processed_mosaic} size {processed.width}x{processed.height} is not divisible "
            f"by original grid {grid_w}x{grid_h}"
        )

    tile_w = processed.width // grid_w
    tile_h = processed.height // grid_h
    start_x = (grid_w - pattern_w) // 2
    start_y = (grid_h - pattern_h) // 2

    for py in range(pattern_h):
        for px in range(pattern_w):
            sprite_id = sprite_ids[py * pattern_w + px]
            left = (start_x + px) * tile_w
            top = (start_y + py) * tile_h
            tile = processed.crop((left, top, left + tile_w, top + tile_h))
            tile = apply_original_alpha(tile, original_folder, sprite_id)
            tile.save(out_dir / f"{sprite_id}.png")

    return len(sprite_ids)


def extract_sheet(
    processed_sheet: Path,
    original_sheet: Path,
    original_folder: Path,
    metadata: dict[str, object],
    out_dir: Path,
) -> int:
    sprite_ids = [int(sprite_id) for sprite_id in metadata["uniqueSprites"]]
    src_tile_w, src_tile_h = get_source_tile_size(original_folder, sprite_ids)
    original_w, original_h = read_image_size(original_sheet)
    cols = original_w // src_tile_w
    rows = original_h // src_tile_h

    if cols <= 0 or rows <= 0:
        raise ValueError(f"Cannot infer sheet grid for {original_sheet}")

    with Image.open(processed_sheet) as image:
        processed = image.convert("RGBA")

    if processed.width % cols != 0 or processed.height % rows != 0:
        raise ValueError(
            f"{processed_sheet} size {processed.width}x{processed.height} is not divisible "
            f"by original sheet grid {cols}x{rows}"
        )

    tile_w = processed.width // cols
    tile_h = processed.height // rows
    for index, sprite_id in enumerate(sprite_ids):
        x = index % cols
        y = index // cols
        left = x * tile_w
        top = y * tile_h
        tile = processed.crop((left, top, left + tile_w, top + tile_h))
        tile = apply_original_alpha(tile, original_folder, sprite_id)
        tile.save(out_dir / f"{sprite_id}.png")

    return len(sprite_ids)


def copy_sprite(processed_root: Path, out_dir: Path, original_folder: Path, sprite_id: int) -> bool:
    source = processed_root / f"sprite-{sprite_id}.png"
    if not source.exists():
        return False
    with Image.open(source) as image:
        tile = apply_original_alpha(image.convert("RGBA"), original_folder, sprite_id)
    tile.save(out_dir / f"{sprite_id}.png")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a CWM from mixed HD map assets: ground mosaics + individual sprites.")
    parser.add_argument("--index", required=True, help="Batch index.json created by prepare_map_hd_assets.py")
    parser.add_argument("--processed-root", required=True, help="Root containing downscaled processed flat PNGs")
    parser.add_argument("--out-sprites", required=True, help="Directory that will receive numeric sprite PNGs")
    parser.add_argument("--cwm", required=True, help="Output Tibia.cwm path")
    parser.add_argument("--sprites-count", type=int, help="Optional CWM sprite count header")
    parser.add_argument("--clean-output", action="store_true", help="Remove numeric PNGs from --out-sprites before writing")
    args = parser.parse_args()

    index_path = Path(args.index)
    processed_root = Path(args.processed_root)
    out_dir = Path(args.out_sprites)
    cwm_path = Path(args.cwm)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.clean_output:
        clean_numeric_output(out_dir)

    index = load_json(index_path)
    if not isinstance(index, list):
        raise ValueError(f"Expected a list in {index_path}")

    copied = 0
    missing: list[int] = []
    seen: set[int] = set()
    for entry in index:
        if not isinstance(entry, dict):
            raise ValueError(f"Invalid index entry in {index_path}")

        client_id = int(entry["clientId"])
        original_folder = Path(entry["folder"])
        metadata_path = original_folder / "metadata.json"
        metadata = load_json(metadata_path)
        if not isinstance(metadata, dict):
            raise ValueError(f"Invalid metadata in {metadata_path}")

        sprite_ids = [int(sprite_id) for sprite_id in metadata["uniqueSprites"]]
        is_ground = bool(metadata.get("isGround", False))
        original_mosaic = original_folder / "mosaic-input.png"
        processed_mosaic = processed_root / f"item-{client_id}-mosaic.png"
        original_sheet = original_folder / "sheet.png"
        processed_sheet = processed_root / f"item-{client_id}-sheet.png"

        if is_ground and original_mosaic.exists() and processed_mosaic.exists():
            copied += extract_mosaic(processed_mosaic, original_mosaic, original_folder, metadata, out_dir)
            seen.update(sprite_ids)
            continue

        if original_sheet.exists() and processed_sheet.exists():
            copied += extract_sheet(processed_sheet, original_sheet, original_folder, metadata, out_dir)
            seen.update(sprite_ids)
            continue

        for sprite_id in sprite_ids:
            if sprite_id in seen:
                continue
            if copy_sprite(processed_root, out_dir, original_folder, sprite_id):
                copied += 1
                seen.add(sprite_id)
            else:
                missing.append(sprite_id)

    if missing:
        sample = ", ".join(str(sprite_id) for sprite_id in missing[:40])
        raise FileNotFoundError(f"Missing {len(missing)} processed sprite PNGs. Sample: {sample}")

    build_cwm(out_dir, cwm_path, args.sprites_count)
    print(f"[ok] prepared {copied} sprites in {out_dir}")
    print(f"[ok] wrote {cwm_path}")


if __name__ == "__main__":
    main()
