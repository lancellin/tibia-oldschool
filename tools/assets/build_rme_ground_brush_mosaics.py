#!/usr/bin/env python3
import argparse
import json
import math
import random
import shutil
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


DEFAULT_DAT = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")
DEFAULT_SPR = Path(r"C:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")
DEFAULT_BRUSH_LOGIC = Path(r"C:\tibia-oldschool\tools\assets\tests\rme-brush-logic-772\brushes.json")
DEFAULT_OUT = Path(r"D:\AI\ComfyUI\input\rme-ground-brush-mosaics-1x")


def safe_name(value: str) -> str:
    return "".join(ch if ch.isalnum() else "-" for ch in value.lower()).strip("-")


def select_brushes(path: Path, brush_names: list[str]) -> list[dict[str, object]]:
    brushes = json.loads(path.read_text(encoding="utf-8"))
    wanted = {name.lower(): name for name in brush_names}
    selected = []
    for brush in brushes:
        name = str(brush.get("name", ""))
        if name.lower() in wanted:
            selected.append(brush)

    found = {str(brush.get("name", "")).lower() for brush in selected}
    missing = [original for key, original in wanted.items() if key not in found]
    if missing:
        raise SystemExit(f"Brushes not found in {path}: {', '.join(missing)}")
    return selected


def base_entries(brush: dict[str, object], include_zero_chance: bool) -> list[dict[str, object]]:
    entries = []
    for item in brush.get("items", []):
        if item.get("kind") != "item":
            continue
        chance = item.get("chance")
        if not include_zero_chance and chance == 0:
            continue
        client_id = item.get("clientId")
        if client_id is None:
            continue
        entries.append({
            "serverId": int(item["serverId"]),
            "clientId": int(client_id),
            "chance": int(chance) if chance is not None else 1,
        })

    unique: dict[int, dict[str, object]] = {}
    for entry in entries:
        unique.setdefault(int(entry["clientId"]), entry)
    return list(unique.values())


def border_entries(brush: dict[str, object], border_ids: list[int]) -> list[dict[str, object]]:
    wanted = set(border_ids)
    entries = []
    for border in brush.get("borders", []):
        border_id = int(border.get("id"))
        if wanted and border_id not in wanted:
            continue
        for item in border.get("items", []):
            client_id = item.get("clientId")
            if client_id is None:
                continue
            entries.append({
                "serverId": int(item["serverId"]),
                "clientId": int(client_id),
                "chance": 1,
                "borderId": border_id,
                "edge": item.get("edge"),
            })

    unique: dict[int, dict[str, object]] = {}
    for entry in entries:
        unique.setdefault(int(entry["clientId"]), entry)
    return list(unique.values())


def decode_sprite_tile(spr_path: Path, sprite_id: int) -> Image.Image:
    sprite = decode_sprite(spr_path, sprite_id, 32, False, False)
    if sprite is None:
        raise ValueError(f"Sprite not found: {sprite_id}")
    return sprite.convert("RGBA")


def compose_item_image(dat_path: Path, spr_path: Path, client_id: int) -> tuple[Image.Image, dict[str, object]]:
    things = parse_dat(dat_path, 772, THING_CATEGORY_ITEM, {client_id})
    if client_id not in things:
        raise ValueError(f"Client id not found in DAT: {client_id}")

    thing = things[client_id]
    width = thing.width
    height = thing.height
    sprites = [sprite_id for sprite_id in thing.sprites[: width * height] if sprite_id > 0]
    if not sprites:
        raise ValueError(f"Client id has no sprites: {client_id}")

    tile_size = 32
    image = Image.new("RGBA", (width * tile_size, height * tile_size), (0, 0, 0, 0))
    for index, sprite_id in enumerate(sprites):
        sprite = decode_sprite(spr_path, sprite_id, tile_size, False, False)
        if sprite is None:
            continue
        x = (index % width) * tile_size
        y = (index // width) * tile_size
        image.alpha_composite(sprite, (x, y))

    metadata = {
        "clientId": client_id,
        "size": {"width": width, "height": height},
        "sprites": sprites,
        "uniqueSprites": thing.unique_sprites,
        "patterns": {"x": thing.pattern_x, "y": thing.pattern_y, "z": thing.pattern_z},
        "isGround": 0 in thing.attrs,
    }
    return image, metadata


def build_sequence(entries: list[dict[str, object]], slots: int, mode: str, seed: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    if mode == "weighted":
        population = []
        for entry in entries:
            weight = max(1, int(entry.get("chance") or 1))
            population.extend([entry] * weight)
        sequence = [rng.choice(population) for _ in range(slots)]
    else:
        sequence = []
        while len(sequence) < slots:
            batch = entries[:]
            rng.shuffle(batch)
            sequence.extend(batch)
        sequence = sequence[:slots]
        rng.shuffle(sequence)
    return sequence


def write_contact_sheet(tiles: list[tuple[int, Image.Image]], path: Path, columns: int = 8) -> None:
    tile_size = 32
    label_h = 12
    rows = math.ceil(len(tiles) / columns)
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_h)), (18, 18, 18, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (client_id, tile) in enumerate(tiles):
        x = (index % columns) * tile_size
        y = (index // columns) * (tile_size + label_h)
        sheet.alpha_composite(tile, (x, y))
        draw.text((x + 1, y + tile_size), str(client_id), fill=(230, 230, 230, 255))
    sheet.save(path)


def build_mosaic_for_brush(
    brush: dict[str, object],
    dat_path: Path,
    spr_path: Path,
    out_root: Path,
    grid: int,
    mode: str,
    seed: int,
    include_zero_chance: bool,
    border_ids: list[int] | None,
    context_sprite_id: int | None,
) -> dict[str, object]:
    entries = border_entries(brush, border_ids) if border_ids else base_entries(brush, include_zero_chance)
    if not entries:
        raise ValueError(f"Brush has no usable entries: {brush.get('name')}")

    brush_name = str(brush["name"])
    if border_ids:
        brush_name = f"{brush_name}-border-{'-'.join(str(border_id) for border_id in border_ids)}"
    brush_dir = out_root / safe_name(brush_name)
    source_dir = brush_dir / "source-tiles"
    brush_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    context_tile = decode_sprite_tile(spr_path, context_sprite_id) if context_sprite_id else None
    tile_images: dict[tuple[int, int], Image.Image] = {}
    item_metadata: dict[int, dict[str, object]] = {}
    variant_entries: list[dict[str, object]] = []
    for entry in entries:
        client_id = int(entry["clientId"])
        _image, metadata = compose_item_image(dat_path, spr_path, client_id)
        item_metadata[client_id] = metadata
        for sprite_id in metadata["uniqueSprites"]:
            variant_key = (client_id, int(sprite_id))
            image = decode_sprite_tile(spr_path, int(sprite_id))
            image.save(source_dir / f"client-{client_id}-sprite-{sprite_id}.png")
            if context_tile:
                display_image = context_tile.copy()
                display_image.alpha_composite(image)
            else:
                display_image = image
            tile_images[variant_key] = display_image
            variant_entries.append({
                **entry,
                "spriteId": int(sprite_id),
            })

    slots = grid * grid
    sequence = build_sequence(variant_entries, slots, mode, seed)
    mosaic = Image.new("RGBA", (grid * 32, grid * 32), (0, 0, 0, 0))
    placements = []
    for index, entry in enumerate(sequence):
        tile_x = index % grid
        tile_y = index // grid
        client_id = int(entry["clientId"])
        sprite_id = int(entry["spriteId"])
        mosaic.alpha_composite(tile_images[(client_id, sprite_id)], (tile_x * 32, tile_y * 32))
        placements.append({
            "tileX": tile_x,
            "tileY": tile_y,
            "x": tile_x * 32,
            "y": tile_y * 32,
            "clientId": client_id,
            "spriteId": sprite_id,
            "serverId": int(entry["serverId"]),
            "chance": int(entry.get("chance") or 1),
        })

    mosaic_path = brush_dir / f"{safe_name(brush_name)}-mosaic-{grid}x{grid}-1x.png"
    mosaic.save(mosaic_path)
    contact_path = brush_dir / f"{safe_name(brush_name)}-source-contact-sheet.png"
    write_contact_sheet(
        [(int(entry["spriteId"]), tile_images[(int(entry["clientId"]), int(entry["spriteId"]))]) for entry in variant_entries],
        contact_path,
    )

    manifest = {
        "brush": brush_name,
        "source": brush.get("source"),
        "type": brush.get("type"),
        "borderIds": border_ids or [],
        "contextSpriteId": context_sprite_id,
        "mode": mode,
        "seed": seed,
        "tileSize": 32,
        "grid": {"width": grid, "height": grid},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_path),
        "entries": entries,
        "tileVariants": variant_entries,
        "itemMetadata": {str(key): value for key, value in item_metadata.items()},
        "placements": placements,
    }
    manifest_path = brush_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return {
        "brush": brush_name,
        "folder": str(brush_dir),
        "mosaic": str(mosaic_path),
        "manifest": str(manifest_path),
        "clientIds": [entry["clientId"] for entry in entries],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build lossless 1x mosaics from RME ground brush logic.")
    parser.add_argument("--brush-logic", type=Path, default=DEFAULT_BRUSH_LOGIC)
    parser.add_argument("--dat", type=Path, default=DEFAULT_DAT)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--brush", action="append", required=True, help="Brush name, e.g. grass or lawn. Can be repeated.")
    parser.add_argument("--grid", type=int, default=24, help="Mosaic grid size in 32px tiles.")
    parser.add_argument("--mode", choices=["balanced", "weighted"], default="balanced")
    parser.add_argument("--seed", type=int, default=772)
    parser.add_argument("--include-zero-chance", action="store_true")
    parser.add_argument("--border-id", action="append", type=int, help="Build only the selected RME border id. Can be repeated.")
    parser.add_argument("--context-sprite-id", type=int, help="Composite transparent tiles over this sprite in the mosaic, while preserving original alpha for recut.")
    parser.add_argument("--keep-existing", action="store_true")
    args = parser.parse_args()

    if args.grid < 4:
        raise SystemExit("--grid must be at least 4")
    if args.out_root.exists() and not args.keep_existing:
        shutil.rmtree(args.out_root)
    args.out_root.mkdir(parents=True, exist_ok=True)

    brushes = select_brushes(args.brush_logic, args.brush)
    summary = [
        build_mosaic_for_brush(
            brush,
            args.dat,
            args.spr,
            args.out_root,
            args.grid,
            args.mode,
            args.seed,
            args.include_zero_chance,
            args.border_id,
            args.context_sprite_id,
        )
        for brush in brushes
    ]
    summary_path = args.out_root / "_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({"outRoot": str(args.out_root), "summary": str(summary_path), "brushes": summary}, indent=2))


if __name__ == "__main__":
    main()
