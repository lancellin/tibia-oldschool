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
DEFAULT_OUT = Path(r"D:\AI\ComfyUI\input\tibia-740-source-mosaics")


def parse_ids(spec: str) -> list[int]:
    ids = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            start, end = [int(value) for value in chunk.split("-", 1)]
            ids.extend(range(start, end + 1))
        else:
            ids.append(int(chunk))
    return list(dict.fromkeys(ids))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 1x mosaic from source client items and map to target client items.")
    parser.add_argument("--name", required=True)
    parser.add_argument("--source-client-ids", required=True)
    parser.add_argument("--target-client-ids", required=True)
    parser.add_argument("--source-dat", type=Path, default=DEFAULT_SOURCE_DAT)
    parser.add_argument("--source-spr", type=Path, default=DEFAULT_SOURCE_SPR)
    parser.add_argument("--source-version", type=int, default=740)
    parser.add_argument("--target-dat", type=Path, default=DEFAULT_TARGET_DAT)
    parser.add_argument("--target-version", type=int, default=772)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--grid", type=int, default=24)
    args = parser.parse_args()

    source_ids = parse_ids(args.source_client_ids)
    target_ids = parse_ids(args.target_client_ids)
    if len(source_ids) != len(target_ids):
        raise SystemExit("--source-client-ids and --target-client-ids must have the same count")

    source_things = parse_dat(args.source_dat, args.source_version, THING_CATEGORY_ITEM, set(source_ids))
    target_things = parse_dat(args.target_dat, args.target_version, THING_CATEGORY_ITEM, set(target_ids))
    missing_source = [client_id for client_id in source_ids if client_id not in source_things]
    missing_target = [client_id for client_id in target_ids if client_id not in target_things]
    if missing_source:
        raise SystemExit(f"Missing source client ids: {missing_source}")
    if missing_target:
        raise SystemExit(f"Missing target client ids: {missing_target}")

    tile_size = 32
    out_dir = args.out_root / args.name
    source_dir = out_dir / "source-tiles"
    out_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    images = {}
    variants = []
    target_sprites = {}
    for source_client_id, target_client_id in zip(source_ids, target_ids):
        source_sprites = source_things[source_client_id].unique_sprites
        target_sprites[str(source_client_id)] = target_things[target_client_id].unique_sprites
        for sprite_id in source_sprites:
            image = decode_sprite(args.source_spr, sprite_id, tile_size, False, False)
            if image is None:
                raise SystemExit(f"Missing source sprite: {sprite_id}")
            image = image.convert("RGBA")
            images[(source_client_id, sprite_id)] = image
            image.save(source_dir / f"client-{source_client_id}-sprite-{sprite_id}.png")
            variants.append({
                "clientId": source_client_id,
                "spriteId": int(sprite_id),
                "serverId": source_client_id,
                "chance": 1,
                "targetClientId": target_client_id,
            })

    mosaic = Image.new("RGBA", (args.grid * tile_size, args.grid * tile_size), (0, 0, 0, 0))
    placements = []
    for index in range(args.grid * args.grid):
        entry = variants[index % len(variants)]
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
        tile_x = index % args.grid
        tile_y = index // args.grid
        mosaic.alpha_composite(images[(client_id, sprite_id)], (tile_x * tile_size, tile_y * tile_size))
        placements.append({
            "tileX": tile_x,
            "tileY": tile_y,
            "x": tile_x * tile_size,
            "y": tile_y * tile_size,
            "clientId": client_id,
            "spriteId": sprite_id,
            "serverId": client_id,
            "chance": 1,
        })

    mosaic_path = out_dir / f"{args.name}-mosaic-{args.grid}x{args.grid}-1x.png"
    mosaic.save(mosaic_path)

    columns = 8
    label_h = 12
    rows = math.ceil(len(variants) / columns)
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_h)), (18, 18, 18, 255))
    draw = ImageDraw.Draw(sheet)
    for index, entry in enumerate(variants):
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_h)
        sheet.alpha_composite(images[(client_id, sprite_id)], (x, y))
        draw.text((x + 1, y + tile_size), f"{client_id}:{sprite_id}", fill=(230, 230, 230, 255))
    contact_path = out_dir / f"{args.name}-source-contact-sheet.png"
    sheet.save(contact_path)

    manifest = {
        "brush": args.name,
        "source": "cross-version-client-item-mosaic",
        "sourceDatVersion": args.source_version,
        "targetDatVersion": args.target_version,
        "type": "item",
        "tileSize": tile_size,
        "grid": {"width": args.grid, "height": args.grid},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_path),
        "entries": [{"clientId": client_id, "serverId": client_id, "chance": 1} for client_id in source_ids],
        "tileVariants": variants,
        "targetSprites": target_sprites,
        "placements": placements,
        "clientMapping": [{"sourceClientId": source, "targetClientId": target} for source, target in zip(source_ids, target_ids)],
    }
    (out_dir / "_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({
        "folder": str(out_dir),
        "mosaic": str(mosaic_path),
        "sourceClientIds": source_ids,
        "targetClientIds": target_ids,
        "sourceVariantCount": len(variants),
        "targetSpriteCount": sum(len(value) for value in target_sprites.values()),
    }, indent=2))


if __name__ == "__main__":
    main()
