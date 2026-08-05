#!/usr/bin/env python3
import argparse
import json
import shutil
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw


CORNER_NW = 1
CORNER_NE = 2
CORNER_SW = 4
CORNER_SE = 8


def find_sprite(root: Path, sprite_id: int) -> Path:
    direct = root / "0001-1000" / f"{sprite_id}.png"
    if direct.exists():
        return direct

    matches = list(root.rglob(f"{sprite_id}.png"))
    if len(matches) != 1:
        raise FileNotFoundError(f"Expected one PNG for sprite {sprite_id}, found {len(matches)}")
    return matches[0]


def build_corner_field(grid: int) -> list[list[bool]]:
    corners = [[False for _ in range(grid + 1)] for _ in range(grid + 1)]

    def fill_rect(left: int, top: int, right: int, bottom: int, value: bool) -> None:
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                corners[y][x] = value

    # Four grass regions create repeated outer corners and long connected edges.
    fill_rect(1, 1, 12, 11, True)
    fill_rect(14, 2, 23, 10, True)
    fill_rect(3, 14, 11, 23, True)
    fill_rect(14, 13, 22, 22, True)

    # One cavity per region repeats all concave pieces without disconnected noise.
    fill_rect(5, 4, 8, 7, False)
    fill_rect(17, 4, 20, 7, False)
    fill_rect(6, 17, 9, 20, False)
    fill_rect(17, 16, 20, 19, False)
    return corners


def tile_pattern(corners: list[list[bool]], x: int, y: int) -> int:
    pattern = 0
    if corners[y][x]:
        pattern |= CORNER_NW
    if corners[y][x + 1]:
        pattern |= CORNER_NE
    if corners[y + 1][x]:
        pattern |= CORNER_SW
    if corners[y + 1][x + 1]:
        pattern |= CORNER_SE
    return pattern


def border_mapping(border_start: int) -> dict[int, int]:
    ids = list(range(border_start, border_start + 12))
    return {
        CORNER_NE | CORNER_SW | CORNER_SE: ids[0],  # missing NW
        CORNER_NW | CORNER_SW | CORNER_SE: ids[1],  # missing NE
        CORNER_NW | CORNER_NE | CORNER_SE: ids[2],  # missing SW
        CORNER_NW | CORNER_NE | CORNER_SW: ids[3],  # missing SE
        CORNER_NW: ids[4],
        CORNER_NE: ids[5],
        CORNER_SE: ids[6],
        CORNER_SW: ids[7],
        CORNER_SW | CORNER_SE: ids[8],  # grass below the edge
        CORNER_NW | CORNER_NE: ids[9],  # grass above the edge
        CORNER_NW | CORNER_SW: ids[10],  # grass left of the edge
        CORNER_NE | CORNER_SE: ids[11],  # grass right of the edge
    }


def write_contact_sheet(images: dict[int, Image.Image], output: Path) -> None:
    ids = sorted(images)
    columns = 4
    cell_w = 64
    cell_h = 48
    rows = (len(ids) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * cell_w, rows * cell_h), (20, 20, 20, 255))
    draw = ImageDraw.Draw(sheet)

    for index, sprite_id in enumerate(ids):
        x = (index % columns) * cell_w
        y = (index // columns) * cell_h
        sheet.alpha_composite(images[sprite_id], (x + 16, y + 2))
        draw.text((x + 3, y + 35), str(sprite_id), fill=(235, 235, 235, 255))
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a connected 24x24-style border mosaic using marching-square corner patterns."
    )
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--base-id", type=int, required=True)
    parser.add_argument("--border-start", type=int, required=True)
    parser.add_argument("--grid", type=int, default=24)
    args = parser.parse_args()

    if args.grid != 24:
        raise SystemExit("The current connected layout is designed for a 24x24 grid")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True, exist_ok=True)

    sprite_ids = [args.base_id, *range(args.border_start, args.border_start + 12)]
    images: dict[int, Image.Image] = {}
    source_paths: dict[int, str] = {}
    for sprite_id in sprite_ids:
        source = find_sprite(args.source_root, sprite_id)
        image = Image.open(source).convert("RGBA")
        if image.size != (32, 32):
            raise ValueError(f"Sprite {sprite_id} is {image.size}, expected 32x32")
        images[sprite_id] = image
        source_paths[sprite_id] = str(source)
        shutil.copy2(source, source_dir / f"{sprite_id}.png")

    mapping = border_mapping(args.border_start)
    corners = build_corner_field(args.grid)
    mosaic = Image.new("RGBA", (args.grid * 32, args.grid * 32), (0, 0, 0, 0))
    placements: list[dict[str, object]] = []
    counts: Counter[int] = Counter()

    for tile_y in range(args.grid):
        for tile_x in range(args.grid):
            pattern = tile_pattern(corners, tile_x, tile_y)
            tile = images[args.base_id].copy()
            sprite_id = mapping.get(pattern)
            if sprite_id is not None:
                tile.alpha_composite(images[sprite_id])
                counts[sprite_id] += 1
            else:
                counts[args.base_id] += 1

            mosaic.alpha_composite(tile, (tile_x * 32, tile_y * 32))
            placements.append(
                {
                    "tileX": tile_x,
                    "tileY": tile_y,
                    "cornerPattern": pattern,
                    "baseSpriteId": args.base_id,
                    "borderSpriteId": sprite_id,
                }
            )

    stem = f"grass{args.base_id}-borders{args.border_start}-{args.border_start + 11}"
    mosaic_path = args.out_dir / f"{stem}-connected-mosaic-{args.grid}x{args.grid}-1x.png"
    contact_path = args.out_dir / f"{stem}-source-contact-sheet.png"
    manifest_path = args.out_dir / "_manifest.json"
    mosaic.save(mosaic_path)
    write_contact_sheet(images, contact_path)

    manifest = {
        "type": "connected-border-mosaic",
        "tileSize": 32,
        "grid": {"width": args.grid, "height": args.grid},
        "baseSpriteId": args.base_id,
        "borderSpriteIds": list(range(args.border_start, args.border_start + 12)),
        "sourcePaths": {str(key): value for key, value in source_paths.items()},
        "patternToSprite": {str(key): value for key, value in mapping.items()},
        "counts": {str(key): value for key, value in sorted(counts.items())},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_path),
        "placements": placements,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(f"[ok] wrote {mosaic_path}")
    print(f"[ok] wrote {manifest_path}")
    print(f"[info] size={mosaic.width}x{mosaic.height}")
    print("[info] counts=" + ", ".join(f"{key}:{value}" for key, value in sorted(counts.items())))


if __name__ == "__main__":
    main()
