#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


DEFAULT_SOURCE_DAT = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\740\Tibia.dat")
DEFAULT_SOURCE_SPR = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\740\Tibia.spr")
DEFAULT_TARGET_DAT = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")


def parse_ids(spec: str) -> list[int]:
    result = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            start, end = [int(value) for value in chunk.split("-", 1)]
            result.extend(range(start, end + 1))
        else:
            result.append(int(chunk))
    return list(dict.fromkeys(result))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a source mosaic from one old client id and map it to target client ids.")
    parser.add_argument("--name", required=True)
    parser.add_argument("--source-client-id", type=int, required=True)
    parser.add_argument("--target-client-ids", required=True, help="Comma/range spec, e.g. 4515-4530,4745,595")
    parser.add_argument("--source-dat", type=Path, default=DEFAULT_SOURCE_DAT)
    parser.add_argument("--source-spr", type=Path, default=DEFAULT_SOURCE_SPR)
    parser.add_argument("--source-version", type=int, default=740)
    parser.add_argument("--target-dat", type=Path, default=DEFAULT_TARGET_DAT)
    parser.add_argument("--target-version", type=int, default=772)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--grid", type=int, default=24)
    args = parser.parse_args()

    source_things = parse_dat(args.source_dat, args.source_version, THING_CATEGORY_ITEM, {args.source_client_id})
    if args.source_client_id not in source_things:
        raise SystemExit(f"Source client id not found: {args.source_client_id}")

    target_client_ids = parse_ids(args.target_client_ids)
    target_things = parse_dat(args.target_dat, args.target_version, THING_CATEGORY_ITEM, set(target_client_ids))
    missing = [client_id for client_id in target_client_ids if client_id not in target_things]
    if missing:
        raise SystemExit(f"Target client ids not found: {', '.join(str(client_id) for client_id in missing)}")

    source_sprites = source_things[args.source_client_id].unique_sprites
    if not source_sprites:
        raise SystemExit(f"Source client id has no sprites: {args.source_client_id}")

    tile_size = 32
    out_dir = args.out_root / args.name
    source_dir = out_dir / "source-tiles"
    out_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    images = {}
    tile_variants = []
    for source_sprite_id in source_sprites:
        image = decode_sprite(args.source_spr, source_sprite_id, tile_size, False, False)
        if image is None:
            raise SystemExit(f"Source sprite not found: {source_sprite_id}")
        image = image.convert("RGBA")
        images[source_sprite_id] = image
        image.save(source_dir / f"client-{args.source_client_id}-sprite-{source_sprite_id}.png")
        tile_variants.append({
            "clientId": args.source_client_id,
            "spriteId": int(source_sprite_id),
            "serverId": args.source_client_id,
            "chance": 1,
        })

    mosaic = Image.new("RGBA", (args.grid * tile_size, args.grid * tile_size), (0, 0, 0, 0))
    placements = []
    for index in range(args.grid * args.grid):
        variant = tile_variants[index % len(tile_variants)]
        sprite_id = int(variant["spriteId"])
        tile_x = index % args.grid
        tile_y = index // args.grid
        mosaic.alpha_composite(images[sprite_id], (tile_x * tile_size, tile_y * tile_size))
        placements.append({
            "tileX": tile_x,
            "tileY": tile_y,
            "x": tile_x * tile_size,
            "y": tile_y * tile_size,
            "clientId": args.source_client_id,
            "spriteId": sprite_id,
            "serverId": args.source_client_id,
            "chance": 1,
        })

    mosaic_path = out_dir / f"{args.name}-mosaic-{args.grid}x{args.grid}-1x.png"
    mosaic.save(mosaic_path)

    label_h = 12
    columns = min(8, len(source_sprites))
    rows = math.ceil(len(source_sprites) / columns)
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_h)), (18, 18, 18, 255))
    draw = ImageDraw.Draw(sheet)
    for index, sprite_id in enumerate(source_sprites):
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_h)
        sheet.alpha_composite(images[sprite_id], (x, y))
        draw.text((x + 1, y + tile_size), str(sprite_id), fill=(230, 230, 230, 255))
    contact_path = out_dir / f"{args.name}-source-contact-sheet.png"
    sheet.save(contact_path)

    target_sprites = {
        str(client_id): target_things[client_id].unique_sprites
        for client_id in target_client_ids
        if target_things[client_id].unique_sprites
    }

    manifest = {
        "brush": args.name,
        "source": "named-cross-source",
        "sourceDatVersion": args.source_version,
        "targetDatVersion": args.target_version,
        "type": "ground",
        "tileSize": tile_size,
        "grid": {"width": args.grid, "height": args.grid},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_path),
        "entries": [{"clientId": args.source_client_id, "serverId": args.source_client_id, "chance": 1}],
        "tileVariants": tile_variants,
        "targetSprites": target_sprites,
        "placements": placements,
    }
    manifest_path = out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({
        "folder": str(out_dir),
        "mosaic": str(mosaic_path),
        "sourceSprites": source_sprites,
        "targetClientIds": target_client_ids,
        "targetSpriteCount": sum(len(value) for value in target_sprites.values()),
    }, indent=2))


if __name__ == "__main__":
    main()
