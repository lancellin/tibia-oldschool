#!/usr/bin/env python3
import argparse
import json
import math
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

from build_rme_native_border_mosaic import (
    TYPE_TO_EDGE,
    build_brush_field,
    expand_border_direction,
    load_global_borders,
    load_rme_border_table,
    neighbour_mask,
    read_otb_mapping,
)
from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(description="Render one original ground Client ID with an RME border family.")
    parser.add_argument("--ground-client-id", type=int, required=True)
    parser.add_argument("--border-id", required=True)
    parser.add_argument("--rme-root", type=Path, required=True)
    parser.add_argument("--version", default="772")
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--grid", type=int, default=24)
    args = parser.parse_args()

    data_dir = args.rme_root / "data" / args.version
    borders = load_global_borders(data_dir / "borders.xml")
    border = borders.get(int(args.border_id))
    if border is None:
        raise SystemExit(f"RME border not found: {args.border_id}")

    server_to_client = read_otb_mapping(data_dir / "items.otb")
    border_client_ids = {
        server_to_client[server_id]
        for server_id in border.tiles.values()
        if server_id in server_to_client
    }
    wanted_client_ids = {args.ground_client_id, *border_client_ids}
    things = parse_dat(args.dat, int(args.version), THING_CATEGORY_ITEM, wanted_client_ids)
    ground = things[args.ground_client_id]
    if ground.width != 1 or ground.height != 1:
        raise ValueError("Ground item must be 1x1")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True, exist_ok=True)
    cache = {}

    def sprite(sprite_id: int) -> Image.Image:
        if sprite_id not in cache:
            image = decode_sprite(args.spr, sprite_id, 32, False, False)
            if image is None:
                raise ValueError(f"Unable to decode sprite {sprite_id}")
            cache[sprite_id] = image.convert("RGBA")
            cache[sprite_id].save(source_dir / f"{sprite_id}.png")
        return cache[sprite_id]

    def ground_sprite_id(x: int, y: int) -> int:
        px = x % ground.pattern_x
        py = y % ground.pattern_y
        index = py * ground.pattern_x + px
        return int(ground.sprites[index])

    border_table = load_rme_border_table(args.rme_root / "source" / "brush_tables.cpp")
    field = build_brush_field(args.grid)
    mosaic = Image.new("RGBA", (args.grid * 32, args.grid * 32), (0, 0, 0, 0))
    placements = []
    edge_counts = Counter()
    border_sprite_ids = set()

    for y in range(args.grid):
        for x in range(args.grid):
            ground_id = ground_sprite_id(x, y)
            tile = sprite(ground_id).copy()
            mask = neighbour_mask(field, x, y) if field[y][x] else 0
            border_items = []
            if mask:
                for direction in border_table[mask]:
                    for rendered_direction, server_id in expand_border_direction(border, direction):
                        client_id = server_to_client[server_id]
                        thing = things[client_id]
                        sprite_id = int(thing.sprites[0])
                        tile.alpha_composite(sprite(sprite_id))
                        edge = TYPE_TO_EDGE[rendered_direction]
                        edge_counts[edge] += 1
                        border_sprite_ids.add(sprite_id)
                        border_items.append({
                            "edge": edge,
                            "direction": rendered_direction,
                            "serverId": server_id,
                            "clientId": client_id,
                            "spriteIds": [sprite_id],
                        })
            mosaic.alpha_composite(tile, (x * 32, y * 32))
            placements.append({
                "tileX": x,
                "tileY": y,
                "groundClientId": args.ground_client_id,
                "groundSpriteIds": [ground_id],
                "neighbourMask": mask,
                "borderItems": border_items,
            })

    stem = f"client-{args.ground_client_id}-border-{args.border_id}"
    mosaic_path = args.out_dir / f"{stem}-rme-native-24x24-1x.png"
    contact_path = args.out_dir / f"{stem}-border-contact-sheet.png"
    mosaic.save(mosaic_path)

    rows = []
    for direction, server_id in sorted(border.tiles.items()):
        client_id = server_to_client[server_id]
        sprite_id = int(things[client_id].sprites[0])
        rows.append((TYPE_TO_EDGE[direction], client_id, sprite_id))
    sheet = Image.new("RGBA", (4 * 128, math.ceil(len(rows) / 4) * 52), (20, 20, 20, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (edge, client_id, sprite_id) in enumerate(rows):
        x = (index % 4) * 128
        y = (index // 4) * 52
        sheet.alpha_composite(sprite(sprite_id), (x + 2, y + 2))
        draw.text((x + 36, y + 5), f"{edge} CID {client_id}", fill=(235, 235, 235, 255))
        draw.text((x + 36, y + 21), f"sprite {sprite_id}", fill=(180, 180, 180, 255))
    sheet.save(contact_path)

    manifest = {
        "type": "rme-native-border-mosaic-v1",
        "mode": "single-original-ground",
        "groundClientId": args.ground_client_id,
        "borderId": args.border_id,
        "borderClientIds": sorted(border_client_ids),
        "groundSpriteIds": ground.unique_sprites,
        "borderSpriteIds": sorted(border_sprite_ids),
        "tileSize": 32,
        "grid": {"width": args.grid, "height": args.grid},
        "mosaic": str(mosaic_path.resolve()),
        "contactSheet": str(contact_path.resolve()),
        "sourceTiles": str(source_dir.resolve()),
        "edgeCounts": dict(sorted(edge_counts.items())),
        "warnings": [],
        "placements": placements,
    }
    manifest_path = args.out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({
        "mosaic": str(mosaic_path),
        "groundClientId": args.ground_client_id,
        "borderClientIds": sorted(border_client_ids),
        "groundSprites": ground.unique_sprites,
        "borderSprites": sorted(border_sprite_ids),
        "edgeCounts": manifest["edgeCounts"],
    }, indent=2))


if __name__ == "__main__":
    main()
