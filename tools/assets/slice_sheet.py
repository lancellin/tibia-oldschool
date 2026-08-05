#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image


def parse_ids(spec: str) -> list[int]:
    return [int(chunk.strip()) for chunk in spec.split(",") if chunk.strip()]


def main() -> None:
    parser = argparse.ArgumentParser(description="Slice a horizontal sprite sheet into individual PNG files.")
    parser.add_argument("--image", required=True, help="Input sheet image")
    parser.add_argument("--out", required=True, help="Output directory")
    parser.add_argument("--ids", required=True, help="Comma-separated sprite ids in left-to-right order")
    parser.add_argument("--tile-width", type=int, required=True, help="Tile width in pixels")
    parser.add_argument("--tile-height", type=int, required=True, help="Tile height in pixels")
    parser.add_argument("--offset-x", type=int, default=0, help="Sheet crop start X")
    parser.add_argument("--offset-y", type=int, default=0, help="Sheet crop start Y")
    parser.add_argument("--stride-x", type=int, help="Horizontal step between tiles; defaults to tile width")
    parser.add_argument("--resize-to", type=int, help="Optional final square size, ex: 32")
    parser.add_argument("--sheet", help="Optional clean output sheet path")
    args = parser.parse_args()

    ids = parse_ids(args.ids)
    stride_x = args.stride_x or args.tile_width
    src = Image.open(args.image).convert("RGBA")

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    tiles: list[Image.Image] = []
    for index, sprite_id in enumerate(ids):
        left = args.offset_x + index * stride_x
        top = args.offset_y
        tile = src.crop((left, top, left + args.tile_width, top + args.tile_height))
        if args.resize_to:
            tile = tile.resize((args.resize_to, args.resize_to), Image.Resampling.LANCZOS)
        tile.save(out_dir / f"{sprite_id}.png")
        tiles.append(tile)
        print(f"[ok] wrote {sprite_id}.png")

    if args.sheet:
        tile_w, tile_h = tiles[0].size
        sheet = Image.new("RGBA", (tile_w * len(tiles), tile_h), (0, 0, 0, 0))
        for index, tile in enumerate(tiles):
            sheet.alpha_composite(tile, (index * tile_w, 0))
        Path(args.sheet).parent.mkdir(parents=True, exist_ok=True)
        sheet.save(args.sheet)
        print(f"[ok] wrote clean sheet to {args.sheet}")


if __name__ == "__main__":
    main()
