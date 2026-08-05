#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


def resolve_path(path: str | None, base_dir: Path) -> Path | None:
    if not path:
        return None
    resolved = Path(path)
    if resolved.is_absolute():
        return resolved
    return base_dir / resolved


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def numeric_pngs(path: Path) -> dict[int, Path]:
    if not path.exists() or not path.is_dir():
        return {}

    result: dict[int, Path] = {}
    for png in sorted(path.glob("*.png")):
        try:
            sprite_id = int(png.stem)
        except ValueError:
            continue
        result[sprite_id] = png
    return result


def item_dirs(processed_root: Path, item_folder_name: str, metadata: dict[str, object]) -> list[Path]:
    client_id = metadata["clientId"]
    category = metadata["category"]
    return [
        processed_root / item_folder_name,
        processed_root / f"{category}-{client_id}",
        processed_root / str(client_id),
    ]


def find_matching_file(processed_root: Path, item_folder_name: str, item_dirs_to_check: list[Path], kind: str) -> Path | None:
    names_by_kind = {
        "mosaic": ["mosaic-output.png", "mosaic-upscaled.png", "mosaic.png", "mosaic-input.png"],
        "sheet": ["sheet-output.png", "sheet-upscaled.png", "sheet.png"],
    }

    for directory in item_dirs_to_check:
        for name in names_by_kind[kind]:
            candidate = directory / name
            if candidate.exists():
                return candidate

        matches = sorted(directory.glob(f"*{kind}*.png"))
        if matches:
            return matches[0]

    root_patterns = [
        f"{item_folder_name}-{kind}*.png",
        f"{item_folder_name}*{kind}*.png",
    ]
    for pattern in root_patterns:
        matches = sorted(processed_root.glob(pattern))
        if matches:
            return matches[0]

    return None


def read_image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def get_source_tile_size(original_folder: Path, sprite_ids: list[int]) -> tuple[int, int]:
    for sprite_id in sprite_ids:
        sprite_path = original_folder / f"{sprite_id}.png"
        if sprite_path.exists():
            return read_image_size(sprite_path)
    raise FileNotFoundError(f"No source sprite PNG found in {original_folder}")


def write_tile(tile: Image.Image, out_dir: Path, sprite_id: int) -> None:
    out_path = out_dir / f"{sprite_id}.png"
    tile.save(out_path)


def extract_mosaic(
    processed_mosaic: Path,
    original_mosaic: Path,
    original_folder: Path,
    metadata: dict[str, object],
    out_dir: Path,
) -> int:
    sprite_ids = list(metadata["uniqueSprites"])
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
            sprite_id = int(sprite_ids[py * pattern_w + px])
            left = (start_x + px) * tile_w
            top = (start_y + py) * tile_h
            tile = processed.crop((left, top, left + tile_w, top + tile_h))
            write_tile(tile, out_dir, sprite_id)

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
        write_tile(tile, out_dir, sprite_id)

    return len(sprite_ids)


def copy_numeric(source_dir: Path, sprite_ids: list[int], out_dir: Path, allow_partial: bool) -> int:
    sources = numeric_pngs(source_dir)
    copied = 0
    missing: list[int] = []

    for sprite_id in sprite_ids:
        source = sources.get(sprite_id)
        if source is None:
            missing.append(sprite_id)
            continue
        shutil.copy2(source, out_dir / f"{sprite_id}.png")
        copied += 1

    if missing and not allow_partial:
        raise FileNotFoundError(f"Missing numeric sprites in {source_dir}: {missing}")

    return copied


def clean_numeric_output(out_dir: Path) -> None:
    if not out_dir.exists():
        return
    for png in out_dir.glob("*.png"):
        if png.stem.isdigit():
            png.unlink()


def process_item(
    entry: dict[str, object],
    index_dir: Path,
    processed_root: Path,
    out_dir: Path,
    fallback_original: bool,
) -> tuple[str, int]:
    original_folder = resolve_path(str(entry["folder"]), index_dir)
    if original_folder is None:
        raise ValueError("Index entry is missing folder")

    metadata_path = original_folder / "metadata.json"
    metadata = load_json(metadata_path)
    if not isinstance(metadata, dict):
        raise ValueError(f"Invalid metadata in {metadata_path}")

    item_folder_name = original_folder.name
    sprites = [int(sprite_id) for sprite_id in metadata["uniqueSprites"]]
    dirs = item_dirs(processed_root, item_folder_name, metadata)

    for directory in dirs:
        available = numeric_pngs(directory)
        if sprites and all(sprite_id in available for sprite_id in sprites):
            return f"numeric:{directory}", copy_numeric(directory, sprites, out_dir, allow_partial=False)

    original_mosaic = resolve_path(entry.get("mosaic"), index_dir)
    processed_mosaic = find_matching_file(processed_root, item_folder_name, dirs, "mosaic")
    if original_mosaic and original_mosaic.exists() and processed_mosaic:
        count = extract_mosaic(processed_mosaic, original_mosaic, original_folder, metadata, out_dir)
        return f"mosaic:{processed_mosaic}", count

    original_sheet = resolve_path(entry.get("sheet"), index_dir)
    processed_sheet = find_matching_file(processed_root, item_folder_name, dirs, "sheet")
    if original_sheet and original_sheet.exists() and processed_sheet:
        count = extract_sheet(processed_sheet, original_sheet, original_folder, metadata, out_dir)
        return f"sheet:{processed_sheet}", count

    if fallback_original:
        return f"fallback:{original_folder}", copy_numeric(original_folder, sprites, out_dir, allow_partial=False)

    raise FileNotFoundError(
        f"No processed assets found for {item_folder_name}. "
        "Provide numeric PNGs, a processed mosaic/sheet, or pass --fallback-original."
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare numeric sprite PNGs from an extracted asset batch and optionally build Tibia.cwm."
    )
    parser.add_argument("--index", required=True, help="Batch index.json created by extract_thing_assets.py")
    parser.add_argument("--processed-root", required=True, help="Root containing processed item folders, sheets, or mosaics")
    parser.add_argument("--out-sprites", required=True, help="Directory that will receive numeric sprite PNGs")
    parser.add_argument("--cwm", help="Optional output Tibia.cwm path")
    parser.add_argument("--sprites-count", type=int, help="Optional CWM sprite count header")
    parser.add_argument("--fallback-original", action="store_true", help="Use original extracted PNGs when no processed asset exists")
    parser.add_argument(
        "--clean-output",
        action="store_true",
        help="Remove numeric PNGs from --out-sprites before writing the new batch",
    )
    args = parser.parse_args()

    index_path = Path(args.index)
    processed_root = Path(args.processed_root)
    out_dir = Path(args.out_sprites)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.clean_output:
        clean_numeric_output(out_dir)

    index = load_json(index_path)
    if not isinstance(index, list):
        raise ValueError(f"Expected a list in {index_path}")

    total = 0
    for entry in index:
        if not isinstance(entry, dict):
            raise ValueError(f"Invalid index entry in {index_path}")
        source, count = process_item(entry, index_path.parent, processed_root, out_dir, args.fallback_original)
        total += count
        print(f"[ok] {entry['clientId']}: {count} sprites from {source}")

    print(f"[ok] prepared {total} sprites in {out_dir}")

    if args.cwm:
        build_cwm(out_dir, Path(args.cwm), args.sprites_count)
        print(f"[ok] wrote {args.cwm}")


if __name__ == "__main__":
    main()
