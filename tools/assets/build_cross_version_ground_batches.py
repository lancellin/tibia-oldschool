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
DEFAULT_OUT = Path(r"D:\AI\ComfyUI\input\tibia-740-source-mosaics\all-common-grounds")


def write_contact_sheet(entries: list[dict[str, int]], tiles: dict[tuple[int, int], Image.Image], path: Path) -> None:
    tile_size = 32
    columns = 8
    label_h = 12
    rows = math.ceil(len(entries) / columns)
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_h)), (18, 18, 18, 255))
    draw = ImageDraw.Draw(sheet)
    for index, entry in enumerate(entries):
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_h)
        sheet.alpha_composite(tiles[(client_id, sprite_id)], (x, y))
        draw.text((x + 1, y + tile_size), f"{client_id}:{sprite_id}", fill=(230, 230, 230, 255))
    sheet.save(path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build batch mosaics from 7.4 grounds that also exist in 7.72.")
    parser.add_argument("--source-dat", type=Path, default=DEFAULT_SOURCE_DAT)
    parser.add_argument("--source-spr", type=Path, default=DEFAULT_SOURCE_SPR)
    parser.add_argument("--source-version", type=int, default=740)
    parser.add_argument("--target-dat", type=Path, default=DEFAULT_TARGET_DAT)
    parser.add_argument("--target-version", type=int, default=772)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--grid", type=int, default=24)
    parser.add_argument("--client-id", type=int, action="append", help="Optional client id filter. Can be repeated.")
    args = parser.parse_args()

    source_items = parse_dat(args.source_dat, args.source_version, THING_CATEGORY_ITEM, None)
    target_items = parse_dat(args.target_dat, args.target_version, THING_CATEGORY_ITEM, None)
    common_ids = sorted(set(source_items) & set(target_items))
    if args.client_id:
        wanted = set(args.client_id)
        common_ids = [client_id for client_id in common_ids if client_id in wanted]

    source_grounds = {client_id: source_items[client_id] for client_id in common_ids if 0 in source_items[client_id].attrs}
    target_grounds = {client_id: target_items[client_id] for client_id in common_ids if 0 in target_items[client_id].attrs}
    client_ids = sorted(set(source_grounds) & set(target_grounds))
    if not client_ids:
        raise SystemExit("No common ground client ids found")

    source_variants: list[dict[str, int]] = []
    target_sprites: dict[str, list[int]] = {}
    item_metadata: dict[str, dict[str, object]] = {}
    for client_id in client_ids:
        source = source_grounds[client_id]
        target = target_grounds[client_id]
        source_unique = source.unique_sprites
        target_unique = target.unique_sprites
        if not source_unique or not target_unique:
            continue
        target_sprites[str(client_id)] = target_unique
        item_metadata[str(client_id)] = {
            "sourceUniqueSprites": source_unique,
            "targetUniqueSprites": target_unique,
            "sourcePatterns": {"x": source.pattern_x, "y": source.pattern_y, "z": source.pattern_z},
            "targetPatterns": {"x": target.pattern_x, "y": target.pattern_y, "z": target.pattern_z},
        }
        for sprite_id in source_unique:
            source_variants.append({"clientId": client_id, "spriteId": int(sprite_id), "serverId": client_id, "chance": 1})

    tile_size = 32
    slots = args.grid * args.grid
    batches = [source_variants[index:index + slots] for index in range(0, len(source_variants), slots)]
    args.out_root.mkdir(parents=True, exist_ok=True)

    summaries = []
    for batch_index, batch in enumerate(batches, 1):
        batch_name = f"grounds-740-to-772-batch-{batch_index:02d}"
        batch_dir = args.out_root / batch_name
        source_dir = batch_dir / "source-tiles"
        batch_dir.mkdir(parents=True, exist_ok=True)
        source_dir.mkdir(parents=True, exist_ok=True)

        tiles: dict[tuple[int, int], Image.Image] = {}
        for entry in batch:
            client_id = int(entry["clientId"])
            sprite_id = int(entry["spriteId"])
            image = decode_sprite(args.source_spr, sprite_id, tile_size, False, False)
            if image is None:
                raise SystemExit(f"Missing source sprite {sprite_id}")
            image = image.convert("RGBA")
            tiles[(client_id, sprite_id)] = image
            image.save(source_dir / f"client-{client_id}-sprite-{sprite_id}.png")

        mosaic = Image.new("RGBA", (args.grid * tile_size, args.grid * tile_size), (0, 0, 0, 0))
        placements = []
        placement_sequence = [batch[index % len(batch)] for index in range(args.grid * args.grid)]
        for index, entry in enumerate(placement_sequence):
            tile_x = index % args.grid
            tile_y = index // args.grid
            client_id = int(entry["clientId"])
            sprite_id = int(entry["spriteId"])
            mosaic.alpha_composite(tiles[(client_id, sprite_id)], (tile_x * tile_size, tile_y * tile_size))
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

        mosaic_path = batch_dir / f"{batch_name}-mosaic-{args.grid}x{args.grid}-1x.png"
        mosaic.save(mosaic_path)
        contact_path = batch_dir / f"{batch_name}-source-contact-sheet.png"
        write_contact_sheet(batch, tiles, contact_path)

        manifest = {
            "brush": batch_name,
            "source": "cross-version-ground-batch",
            "sourceDatVersion": args.source_version,
            "targetDatVersion": args.target_version,
            "type": "ground",
            "tileSize": tile_size,
            "grid": {"width": args.grid, "height": args.grid},
            "mosaic": str(mosaic_path),
            "contactSheet": str(contact_path),
            "entries": [{"clientId": client_id, "serverId": client_id, "chance": 1} for client_id in client_ids],
            "tileVariants": batch,
            "targetSprites": target_sprites,
            "itemMetadata": item_metadata,
            "placements": placements,
        }
        manifest_path = batch_dir / "_manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        summaries.append({
            "batch": batch_name,
            "folder": str(batch_dir),
            "mosaic": str(mosaic_path),
            "sourceVariantCount": len(batch),
        })

    summary = {
        "sourceDatVersion": args.source_version,
        "targetDatVersion": args.target_version,
        "clientIdCount": len(client_ids),
        "sourceVariantCount": len(source_variants),
        "targetSpriteCount": sum(len(value) for value in target_sprites.values()),
        "batchCount": len(summaries),
        "batches": summaries,
    }
    summary_path = args.out_root / "_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
