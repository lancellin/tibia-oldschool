#!/usr/bin/env python3
import argparse
import json
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


RECTANGLES = (
    (1, 1, 11, 10),
    (14, 2, 22, 9),
    (2, 14, 10, 22),
    (13, 13, 22, 22),
)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build rectangular floor mosaics with eight external border pieces.")
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--base-client-id", type=int, required=True)
    parser.add_argument("--north", type=int, required=True)
    parser.add_argument("--east", type=int, required=True)
    parser.add_argument("--south", type=int, required=True)
    parser.add_argument("--west", type=int, required=True)
    parser.add_argument("--north-west", type=int, required=True)
    parser.add_argument("--north-east", type=int, required=True)
    parser.add_argument("--south-west", type=int, required=True)
    parser.add_argument("--south-east", type=int, required=True)
    args = parser.parse_args()

    client_by_role = {
        "base": args.base_client_id,
        "n": args.north,
        "e": args.east,
        "s": args.south,
        "w": args.west,
        "nw": args.north_west,
        "ne": args.north_east,
        "sw": args.south_west,
        "se": args.south_east,
    }
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, set(client_by_role.values()))
    sprite_by_role = {}
    images = {}
    args.out_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True, exist_ok=True)

    for role, client_id in client_by_role.items():
        thing = things[client_id]
        sprite_ids = [int(sprite_id) for sprite_id in thing.unique_sprites if int(sprite_id) > 0]
        if len(sprite_ids) != 1:
            raise ValueError(f"Client ID {client_id} must resolve to one sprite, got {sprite_ids}")
        sprite_id = sprite_ids[0]
        image = decode_sprite(args.spr, sprite_id, 32, False, False)
        if image is None:
            raise ValueError(f"Unable to decode sprite {sprite_id} for Client ID {client_id}")
        images[role] = image.convert("RGBA")
        sprite_by_role[role] = sprite_id
        images[role].save(source_dir / f"{sprite_id}.png")

    grid = 24
    mosaic = Image.new("RGBA", (grid * 32, grid * 32), (0, 0, 0, 0))
    placements = []
    counts = Counter()
    rectangle_lookup = {}
    for left, top, right, bottom in RECTANGLES:
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                rectangle_lookup[(x, y)] = (left, top, right, bottom)

    for y in range(grid):
        for x in range(grid):
            tile = images["base"].copy()
            role = None
            bounds = rectangle_lookup.get((x, y))
            if bounds:
                left, top, right, bottom = bounds
                if x == left and y == top:
                    role = "nw"
                elif x == right and y == top:
                    role = "ne"
                elif x == left and y == bottom:
                    role = "sw"
                elif x == right and y == bottom:
                    role = "se"
                elif y == top:
                    role = "n"
                elif y == bottom:
                    role = "s"
                elif x == left:
                    role = "w"
                elif x == right:
                    role = "e"

            border_sprite_id = None
            if role:
                tile.alpha_composite(images[role])
                border_sprite_id = sprite_by_role[role]
                counts[border_sprite_id] += 1
            else:
                counts[sprite_by_role["base"]] += 1

            mosaic.alpha_composite(tile, (x * 32, y * 32))
            placements.append({
                "tileX": x,
                "tileY": y,
                "baseSpriteId": sprite_by_role["base"],
                "borderSpriteId": border_sprite_id,
                "role": role or "base",
            })

    stem = f"client-{args.base_client_id}-rectangle-border"
    mosaic_path = args.out_dir / f"{stem}-24x24-1x.png"
    contact_path = args.out_dir / f"{stem}-contact-sheet.png"
    mosaic.save(mosaic_path)

    roles = ["base", "n", "e", "s", "w", "nw", "ne", "sw", "se"]
    sheet = Image.new("RGBA", (3 * 96, 3 * 64), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for index, role in enumerate(roles):
        x = (index % 3) * 96
        y = (index // 3) * 64
        sheet.alpha_composite(images[role], (x + 32, y + 2))
        draw.text((x + 4, y + 38), f"{role} CID {client_by_role[role]}", fill=(235, 235, 235, 255))
    sheet.save(contact_path)

    manifest = {
        "type": "connected-border-mosaic",
        "mode": "external-rectangles",
        "tileSize": 32,
        "grid": {"width": grid, "height": grid},
        "baseClientId": args.base_client_id,
        "baseSpriteId": sprite_by_role["base"],
        "borderClientIds": {role: client_by_role[role] for role in roles if role != "base"},
        "borderSpriteIds": [sprite_by_role[role] for role in roles if role != "base"],
        "roleToSprite": sprite_by_role,
        "counts": {str(key): value for key, value in sorted(counts.items())},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_path),
        "placements": placements,
    }
    (args.out_dir / "_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"mosaic": str(mosaic_path), "sprites": sprite_by_role, "counts": manifest["counts"]}, indent=2))


if __name__ == "__main__":
    main()
