#!/usr/bin/env python3
import argparse
import json
import random
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite


DEFAULT_SPR = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")
DEFAULT_OUT = Path(r"D:\tibia-oldschool\tools\assets\work\ground-mosaic-tests")


def parse_sprite_range(value: str) -> list[int]:
    sprite_ids: list[int] = []
    for part in value.split(","):
        token = part.strip()
        if not token:
            continue
        if "-" in token:
            start_text, end_text = token.split("-", 1)
            start = int(start_text)
            end = int(end_text)
            if end < start:
                raise argparse.ArgumentTypeError(f"Invalid descending range: {token}")
            sprite_ids.extend(range(start, end + 1))
        else:
            sprite_ids.append(int(token))
    if not sprite_ids:
        raise argparse.ArgumentTypeError("At least one sprite id is required")
    if len(set(sprite_ids)) != len(sprite_ids):
        raise argparse.ArgumentTypeError("Sprite ids must be unique")
    return sprite_ids


def parse_client_split(value: str) -> tuple[int, int, int]:
    parts = value.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("Expected CLIENT_ID:START_SPRITE:END_SPRITE")
    client_id, start, end = (int(part) for part in parts)
    if end < start:
        raise argparse.ArgumentTypeError(f"Invalid descending range: {value}")
    return client_id, start, end


def build_mosaic(
    name: str,
    sprite_ids: list[int],
    client_splits: list[tuple[int, int, int]],
    spr_path: Path,
    out_root: Path,
    grid: int,
    seed: int,
) -> dict[str, object]:
    tile_count = grid * grid
    if tile_count % len(sprite_ids) != 0:
        raise SystemExit(
            f"{grid}x{grid} has {tile_count} tiles, which is not divisible by "
            f"{len(sprite_ids)} sprite variants"
        )

    sprite_clients: dict[int, int] = {}
    for client_id, start, end in client_splits:
        for sprite_id in range(start, end + 1):
            if sprite_id in sprite_clients:
                raise SystemExit(f"Sprite {sprite_id} is assigned to more than one client id")
            sprite_clients[sprite_id] = client_id
    missing_clients = [sprite_id for sprite_id in sprite_ids if sprite_id not in sprite_clients]
    extra_clients = [sprite_id for sprite_id in sprite_clients if sprite_id not in sprite_ids]
    if missing_clients or extra_clients:
        raise SystemExit(
            f"Client split mismatch; missing={missing_clients or 'none'}, "
            f"extra={extra_clients or 'none'}"
        )

    tile_size = 32
    out_dir = out_root / name
    source_dir = out_dir / "source-tiles"
    out_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    images: dict[int, Image.Image] = {}
    tile_variants: list[dict[str, int]] = []
    for sprite_id in sprite_ids:
        image = decode_sprite(spr_path, sprite_id, tile_size, False, False)
        if image is None:
            raise SystemExit(f"Sprite not found: {sprite_id}")
        image = image.convert("RGBA")
        images[sprite_id] = image
        client_id = sprite_clients[sprite_id]
        image.save(source_dir / f"client-{client_id}-sprite-{sprite_id}.png")
        tile_variants.append(
            {
                "serverId": client_id,
                "clientId": client_id,
                "chance": 1,
                "spriteId": sprite_id,
            }
        )

    repetitions = tile_count // len(sprite_ids)
    sequence = sprite_ids * repetitions
    random.Random(seed).shuffle(sequence)

    mosaic = Image.new("RGBA", (grid * tile_size, grid * tile_size), (0, 0, 0, 0))
    placements: list[dict[str, int]] = []
    for index, sprite_id in enumerate(sequence):
        tile_x = index % grid
        tile_y = index // grid
        client_id = sprite_clients[sprite_id]
        mosaic.alpha_composite(images[sprite_id], (tile_x * tile_size, tile_y * tile_size))
        placements.append(
            {
                "tileX": tile_x,
                "tileY": tile_y,
                "x": tile_x * tile_size,
                "y": tile_y * tile_size,
                "serverId": client_id,
                "clientId": client_id,
                "chance": 1,
                "spriteId": sprite_id,
            }
        )

    mosaic_path = out_dir / f"{name}-mosaic-{grid}x{grid}-1x.png"
    mosaic.save(mosaic_path)

    columns = min(8, len(sprite_ids))
    rows = (len(sprite_ids) + columns - 1) // columns
    label_height = 13
    sheet = Image.new(
        "RGBA",
        (columns * tile_size, rows * (tile_size + label_height)),
        (18, 18, 18, 255),
    )
    draw = ImageDraw.Draw(sheet)
    for index, sprite_id in enumerate(sprite_ids):
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_height)
        sheet.alpha_composite(images[sprite_id], (x, y))
        draw.text((x + 1, y + tile_size), str(sprite_id), fill=(230, 230, 230, 255))
    contact_sheet_path = out_dir / f"{name}-source-contact-sheet.png"
    sheet.save(contact_sheet_path)

    client_ids = list(dict.fromkeys(sprite_clients[sprite_id] for sprite_id in sprite_ids))
    entries = [
        {"serverId": client_id, "clientId": client_id, "chance": 1}
        for client_id in client_ids
    ]
    counts = Counter(sequence)
    manifest = {
        "brush": name,
        "source": "manual-contiguous-sprite-family",
        "datVersion": 772,
        "type": "ground",
        "borderIds": [],
        "contextSpriteId": None,
        "mode": "balanced-sprite-family",
        "seed": seed,
        "tileSize": tile_size,
        "grid": {"width": grid, "height": grid},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_sheet_path),
        "entries": entries,
        "tileVariants": tile_variants,
        "itemMetadata": {},
        "variantCounts": {str(sprite_id): counts[sprite_id] for sprite_id in sprite_ids},
        "placements": placements,
    }
    manifest_path = out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    return {
        "folder": str(out_dir),
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_sheet_path),
        "manifest": str(manifest_path),
        "clientIds": client_ids,
        "spriteIds": sprite_ids,
        "variantCount": len(sprite_ids),
        "occurrencesPerVariant": repetitions,
        "size": [mosaic.width, mosaic.height],
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a balanced mosaic from an explicit contiguous sprite family."
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--sprite-ids", type=parse_sprite_range, required=True)
    parser.add_argument(
        "--client-split",
        type=parse_client_split,
        action="append",
        required=True,
        help="Sprite ownership as CLIENT_ID:START_SPRITE:END_SPRITE; repeat as needed.",
    )
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--grid", type=int, default=24)
    parser.add_argument("--seed", type=int, default=106109)
    args = parser.parse_args()

    if args.grid < 4:
        raise SystemExit("--grid must be at least 4")
    result = build_mosaic(
        args.name,
        args.sprite_ids,
        args.client_split,
        args.spr,
        args.out_root,
        args.grid,
        args.seed,
    )
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
