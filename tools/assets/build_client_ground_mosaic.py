#!/usr/bin/env python3
import argparse
import json
import random
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


DEFAULT_DAT = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")
DEFAULT_SPR = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")
DEFAULT_OUT = Path(r"D:\AI\ComfyUI\input\rme-ground-brush-mosaics-1x-v2")


def build_client_mosaic(
    client_ids: list[int],
    dat_path: Path,
    spr_path: Path,
    dat_version: int,
    out_root: Path,
    grid: int,
    seed: int,
) -> dict[str, object]:
    things = parse_dat(dat_path, dat_version, THING_CATEGORY_ITEM, set(client_ids))
    missing = [client_id for client_id in client_ids if client_id not in things]
    if missing:
        raise SystemExit(f"Client ids not found in DAT: {', '.join(str(client_id) for client_id in missing)}")

    tile_size = 32
    if len(client_ids) == 1:
        folder_name = f"client-{client_ids[0]}"
    elif client_ids == list(range(client_ids[0], client_ids[-1] + 1)):
        folder_name = f"clients-{client_ids[0]}-{client_ids[-1]}"
    else:
        folder_name = "clients-" + "-".join(str(client_id) for client_id in client_ids)
    out_dir = out_root / folder_name
    source_dir = out_dir / "source-tiles"
    out_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    images: dict[tuple[int, int], Image.Image] = {}
    item_metadata: dict[str, dict[str, object]] = {}
    tile_variants: list[dict[str, object]] = []
    entries = []
    for client_id in client_ids:
        thing = things[client_id]
        sprite_ids = [
            int(sprite_id)
            for sprite_id in thing.sprites[: thing.pattern_x * thing.pattern_y * thing.pattern_z]
            if int(sprite_id) > 0
        ]
        if not sprite_ids:
            raise SystemExit(f"Client id has no sprites: {client_id}")

        unique_sprites = list(dict.fromkeys(sprite_ids))
        entries.append({"serverId": client_id, "clientId": client_id, "chance": 1})
        item_metadata[str(client_id)] = {
            "clientId": client_id,
            "size": {"width": thing.width, "height": thing.height},
            "sprites": sprite_ids,
            "uniqueSprites": unique_sprites,
            "patterns": {"x": thing.pattern_x, "y": thing.pattern_y, "z": thing.pattern_z},
            "isGround": 0 in thing.attrs,
        }
        for sprite_id in unique_sprites:
            image = decode_sprite(spr_path, sprite_id, tile_size, False, False)
            if image is None:
                raise SystemExit(f"Sprite not found: {sprite_id}")
            image = image.convert("RGBA")
            images[(client_id, sprite_id)] = image
            image.save(source_dir / f"client-{client_id}-sprite-{sprite_id}.png")
            tile_variants.append({
                "serverId": client_id,
                "clientId": client_id,
                "chance": 1,
                "spriteId": sprite_id,
            })

    mosaic = Image.new("RGBA", (grid * tile_size, grid * tile_size), (0, 0, 0, 0))
    placements = []
    if len(client_ids) == 1:
        client_id = client_ids[0]
        pattern_w = int(things[client_id].pattern_x)
        pattern_h = int(things[client_id].pattern_y)
        pattern_sprites = item_metadata[str(client_id)]["sprites"]
        sequence = []
        for tile_y in range(grid):
            for tile_x in range(grid):
                pattern_x = tile_x % pattern_w
                pattern_y = tile_y % pattern_h
                sequence.append({
                    "tileX": tile_x,
                    "tileY": tile_y,
                    "clientId": client_id,
                    "spriteId": int(pattern_sprites[pattern_y * pattern_w + pattern_x]),
                })
    else:
        rng = random.Random(seed)
        sequence = []
        variants = tile_variants[:]
        while len(sequence) < grid * grid:
            batch = [variant.copy() for variant in variants]
            rng.shuffle(batch)
            sequence.extend(batch)
        sequence = sequence[: grid * grid]
        rng.shuffle(sequence)
        for index, entry in enumerate(sequence):
            entry["tileX"] = index % grid
            entry["tileY"] = index // grid

    for entry in sequence:
        tile_x = int(entry["tileX"])
        tile_y = int(entry["tileY"])
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
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

    mosaic_path = out_dir / f"{folder_name}-mosaic-{grid}x{grid}-1x.png"
    mosaic.save(mosaic_path)

    columns = min(8, max(1, len(tile_variants)))
    unique_sheet_entries = [(int(entry["clientId"]), int(entry["spriteId"])) for entry in tile_variants]
    rows = (len(unique_sheet_entries) + columns - 1) // columns
    label_h = 12
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_h)), (18, 18, 18, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (client_id, sprite_id) in enumerate(unique_sheet_entries):
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_h)
        sheet.alpha_composite(images[(client_id, sprite_id)], (x, y))
        draw.text((x + 1, y + tile_size), str(sprite_id), fill=(230, 230, 230, 255))
    sheet_path = out_dir / f"{folder_name}-source-contact-sheet.png"
    sheet.save(sheet_path)

    manifest = {
        "brush": folder_name,
        "source": "manual-client-id",
        "datVersion": dat_version,
        "type": "ground",
        "borderIds": [],
        "contextSpriteId": None,
        "mode": "pattern" if len(client_ids) == 1 else "balanced",
        "seed": seed,
        "tileSize": tile_size,
        "grid": {"width": grid, "height": grid},
        "mosaic": str(mosaic_path),
        "contactSheet": str(sheet_path),
        "entries": entries,
        "tileVariants": tile_variants,
        "itemMetadata": item_metadata,
        "placements": placements,
    }
    manifest_path = out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return {
        "folder": str(out_dir),
        "mosaic": str(mosaic_path),
        "manifest": str(manifest_path),
        "sprites": [int(entry["spriteId"]) for entry in tile_variants],
        "size": [mosaic.width, mosaic.height],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a repeated 1x mosaic for one DAT client ground id.")
    parser.add_argument("--client-id", type=int, action="append", required=True, help="Client id. Can be repeated for a combined mosaic.")
    parser.add_argument("--dat", type=Path, default=DEFAULT_DAT)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--dat-version", type=int, default=772)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--grid", type=int, default=24)
    parser.add_argument("--seed", type=int, default=772)
    args = parser.parse_args()

    if args.grid < 4:
        raise SystemExit("--grid must be at least 4")
    print(json.dumps(
        build_client_mosaic(args.client_id, args.dat, args.spr, args.dat_version, args.out_root, args.grid, args.seed),
        indent=2,
    ))


if __name__ == "__main__":
    main()
