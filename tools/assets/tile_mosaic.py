#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image


def parse_ids(spec: str) -> list[int]:
    return [int(chunk.strip()) for chunk in spec.split(",") if chunk.strip()]


def load_tiles(tile_dir: Path, ids: list[int]) -> list[Image.Image]:
    tiles: list[Image.Image] = []
    for sprite_id in ids:
        path = tile_dir / f"{sprite_id}.png"
        if not path.exists():
            raise FileNotFoundError(f"Missing tile: {path}")
        tiles.append(Image.open(path).convert("RGBA"))
    return tiles


def build_mosaic(args: argparse.Namespace) -> None:
    ids = parse_ids(args.ids)
    if len(ids) != args.pattern_width * args.pattern_height:
        raise ValueError("ids count must match pattern-width * pattern-height")

    tile_dir = Path(args.tiles)
    out_path = Path(args.out)

    tiles = load_tiles(tile_dir, ids)
    tile_w, tile_h = tiles[0].size
    for tile in tiles[1:]:
        if tile.size != (tile_w, tile_h):
            raise ValueError("All source tiles must share the same size")

    mosaic = Image.new("RGBA", (args.repeat_x * tile_w, args.repeat_y * tile_h), (0, 0, 0, 0))
    for gy in range(args.repeat_y):
        for gx in range(args.repeat_x):
            pattern_x = gx % args.pattern_width
            pattern_y = gy % args.pattern_height
            tile_index = pattern_y * args.pattern_width + pattern_x
            mosaic.alpha_composite(tiles[tile_index], (gx * tile_w, gy * tile_h))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    mosaic.save(out_path)
    print(f"[ok] wrote mosaic to {out_path}")
    print(f"[info] size={mosaic.width}x{mosaic.height}, tile={tile_w}x{tile_h}")


def extract_center_pattern(args: argparse.Namespace) -> None:
    ids = parse_ids(args.ids)
    if len(ids) != args.pattern_width * args.pattern_height:
        raise ValueError("ids count must match pattern-width * pattern-height")

    src = Image.open(args.image).convert("RGBA")
    tile_w = args.tile_width
    tile_h = args.tile_height

    grid_w = src.width // tile_w
    grid_h = src.height // tile_h
    if grid_w < args.pattern_width or grid_h < args.pattern_height:
        raise ValueError("Image is too small for the requested pattern/tile size")

    start_tile_x = (grid_w - args.pattern_width) // 2
    start_tile_y = (grid_h - args.pattern_height) // 2

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    tiles: list[Image.Image] = []
    for py in range(args.pattern_height):
        for px in range(args.pattern_width):
            sprite_id = ids[py * args.pattern_width + px]
            left = (start_tile_x + px) * tile_w
            top = (start_tile_y + py) * tile_h
            tile = src.crop((left, top, left + tile_w, top + tile_h))
            tile.save(out_dir / f"{sprite_id}.png")
            tiles.append(tile)
            print(f"[ok] wrote {sprite_id}.png")

    if args.sheet:
        sheet = Image.new("RGBA", (tile_w * args.pattern_width, tile_h * args.pattern_height), (0, 0, 0, 0))
        for py in range(args.pattern_height):
            for px in range(args.pattern_width):
                tile = tiles[py * args.pattern_width + px]
                sheet.alpha_composite(tile, (px * tile_w, py * tile_h))
        sheet_path = Path(args.sheet)
        sheet_path.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(sheet_path)
        print(f"[ok] wrote sheet to {sheet_path}")

    print(
        f"[info] extracted centered pattern from tile grid {grid_w}x{grid_h} "
        f"starting at tile ({start_tile_x}, {start_tile_y})"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build repeated mosaics and extract center pattern tiles.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build", help="Build a repeated mosaic from a pattern of tiles")
    build_parser.add_argument("--tiles", required=True, help="Directory containing per-sprite PNGs")
    build_parser.add_argument("--ids", required=True, help="Comma-separated sprite ids in row-major pattern order")
    build_parser.add_argument("--pattern-width", type=int, required=True, help="Pattern width in tiles")
    build_parser.add_argument("--pattern-height", type=int, required=True, help="Pattern height in tiles")
    build_parser.add_argument("--repeat-x", type=int, default=8, help="Mosaic width in tiles")
    build_parser.add_argument("--repeat-y", type=int, default=8, help="Mosaic height in tiles")
    build_parser.add_argument("--out", required=True, help="Output mosaic image")
    build_parser.set_defaults(func=build_mosaic)

    extract_parser = subparsers.add_parser("extract", help="Extract the centered pattern back into tiles")
    extract_parser.add_argument("--image", required=True, help="Upscaled repeated mosaic image")
    extract_parser.add_argument("--ids", required=True, help="Comma-separated sprite ids in row-major pattern order")
    extract_parser.add_argument("--pattern-width", type=int, required=True, help="Pattern width in tiles")
    extract_parser.add_argument("--pattern-height", type=int, required=True, help="Pattern height in tiles")
    extract_parser.add_argument("--tile-width", type=int, required=True, help="Final tile width in pixels")
    extract_parser.add_argument("--tile-height", type=int, required=True, help="Final tile height in pixels")
    extract_parser.add_argument("--out", required=True, help="Directory to write extracted PNGs")
    extract_parser.add_argument("--sheet", help="Optional output sheet path")
    extract_parser.set_defaults(func=extract_center_pattern)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
