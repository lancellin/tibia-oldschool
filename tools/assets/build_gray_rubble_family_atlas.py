#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


TILE_SIZE = 32
FAMILY_CLIENT_IDS = {1305, 1306, 1307, 1308, 1310, 1311, 1312, 1313, 1314, 1315}
REPRESENTATIVES = (
    {
        "clientId": 1305,
        "label": "vertical-pattern-a",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 0,
    },
    {
        "clientId": 1306,
        "label": "horizontal-pattern-a",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 0,
    },
    {
        "clientId": 1307,
        "label": "diagonal-forward",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 0,
    },
    {
        "clientId": 1311,
        "label": "diagonal-reverse",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 0,
    },
    {
        "clientId": 1305,
        "label": "vertical-pattern-b",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 1,
    },
    {
        "clientId": 1306,
        "label": "horizontal-pattern-b",
        "repeatX": 6,
        "repeatY": 6,
        "targetVariant": 1,
    },
)

PANEL_COLUMNS = 3

LEVELS = (0, -1, -2)


def render_variant(thing, spr_path: Path, variant_x: int, variant_y: int) -> tuple[Image.Image, list[int]]:
    image = Image.new("RGBA", (thing.width * TILE_SIZE, thing.height * TILE_SIZE), (0, 0, 0, 0))
    sprite_ids: list[int] = []
    pattern_index = variant_y * thing.pattern_x + variant_x
    start = pattern_index * thing.width * thing.height
    end = start + thing.width * thing.height
    for index, sprite_id in enumerate(thing.sprites[start:end]):
        sprite_id = int(sprite_id)
        sprite_ids.append(sprite_id)
        if sprite_id <= 0:
            continue
        sprite = decode_sprite(spr_path, sprite_id, TILE_SIZE, False, False)
        if sprite is None:
            raise ValueError(f"Unable to decode sprite {sprite_id}")
        x = (index % thing.width) * TILE_SIZE
        y = (index // thing.width) * TILE_SIZE
        image.alpha_composite(sprite.convert("RGBA"), (x, y))
    return image, sprite_ids


def checkerboard(size: tuple[int, int], cell: int = 16) -> Image.Image:
    image = Image.new("RGBA", size, (42, 42, 42, 255))
    draw = ImageDraw.Draw(image)
    colors = ((46, 46, 46, 255), (68, 68, 68, 255))
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            draw.rectangle((x, y, x + cell - 1, y + cell - 1), fill=colors[((x // cell) + (y // cell)) % 2])
    return image


def project(map_x: int, map_y: int, map_z: int) -> tuple[int, int]:
    floor_delta = -map_z
    return (
        (map_x - floor_delta) * TILE_SIZE,
        (map_y - floor_delta) * TILE_SIZE,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a continuity atlas for the gray rubble family around CIDs 1305-1315.")
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    args = parser.parse_args()

    if args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True)

    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, FAMILY_CLIENT_IDS)
    rendered: dict[int, list[dict[str, object]]] = {}

    for client_id in sorted(FAMILY_CLIENT_IDS):
        thing = things[client_id]
        variants = []
        for py in range(max(1, thing.pattern_y)):
            for px in range(max(1, thing.pattern_x)):
                image, sprite_ids = render_variant(thing, args.spr, px, py)
                variants.append(
                    {
                        "variantX": px,
                        "variantY": py,
                        "image": image,
                        "spriteIds": sprite_ids,
                    }
                )
                image.save(source_dir / f"client-{client_id}-variant-{px}-{py}.png")
        rendered[client_id] = variants
        for sprite_id in thing.unique_sprites:
            sprite = decode_sprite(args.spr, int(sprite_id), TILE_SIZE, False, False)
            if sprite is None:
                raise ValueError(f"Unable to decode sprite {sprite_id}")
            sprite.convert("RGBA").save(source_dir / f"{int(sprite_id)}.png")

    panel_width = 512
    panel_height = 512
    gap = 32
    panel_count = len(REPRESENTATIVES)
    panel_rows_count = (panel_count + PANEL_COLUMNS - 1) // PANEL_COLUMNS
    atlas_width = panel_width * PANEL_COLUMNS + gap * (PANEL_COLUMNS - 1)
    atlas_height = panel_height * panel_rows_count + gap * (panel_rows_count - 1)
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    panel_rows = []
    target_placements = []

    for index, spec in enumerate(REPRESENTATIVES):
        cx = index % PANEL_COLUMNS
        cy = index // PANEL_COLUMNS
        panel_x = cx * (panel_width + gap)
        panel_y = cy * (panel_height + gap)
        panel_rows.append({"clientId": spec["clientId"], "label": spec["label"], "x": panel_x, "y": panel_y, "width": panel_width, "height": panel_height})
        variant = rendered[spec["clientId"]][int(spec["targetVariant"])]
        tile = variant["image"]
        tile_w, tile_h = tile.size

        projected = []
        half_x = int(spec["repeatX"]) // 2
        half_y = int(spec["repeatY"]) // 2
        for z in LEVELS:
            for row in range(int(spec["repeatY"])):
                for col in range(int(spec["repeatX"])):
                    map_x = col - half_x
                    map_y = row - half_y
                    screen_x, screen_y = project(map_x, map_y, z)
                    projected.append(
                        {
                            "z": z,
                            "mapX": map_x,
                            "mapY": map_y,
                            "screenX": screen_x,
                            "screenY": screen_y,
                        }
                    )

        min_x = min(item["screenX"] for item in projected)
        min_y = min(item["screenY"] for item in projected)
        pad = TILE_SIZE * 2
        for item in sorted(projected, key=lambda value: value["z"], reverse=True):
            x = panel_x + item["screenX"] - min_x + pad
            y = panel_y + item["screenY"] - min_y + pad
            atlas.alpha_composite(tile, (x, y))

        target_entry = next(
            item for item in projected if item["z"] == -1 and item["mapX"] == 0 and item["mapY"] == 0
        )
        target_pos = (
            panel_x + target_entry["screenX"] - min_x + pad,
            panel_y + target_entry["screenY"] - min_y + pad,
        )
        atlas.alpha_composite(tile, target_pos)
        target_placements.append(
            {
                "clientId": int(spec["clientId"]),
                "label": str(spec["label"]),
                "variantX": int(variant["variantX"]),
                "variantY": int(variant["variantY"]),
                "object": {"x": int(target_pos[0]), "y": int(target_pos[1]), "width": tile_w, "height": tile_h},
                "spriteIds": [int(value) for value in variant["spriteIds"]],
                "size": {"width": int(things[spec["clientId"]].width), "height": int(things[spec["clientId"]].height)},
            }
        )

    input_path = args.out_dir / "gray-rubble-family-atlas-input.png"
    atlas.save(input_path)

    preview = checkerboard(atlas.size)
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for row in panel_rows:
        draw.rectangle((row["x"], row["y"], row["x"] + row["width"] - 1, row["y"] + row["height"] - 1), outline=(220, 220, 220, 180))
        draw.text((row["x"] + 6, row["y"] + 4), f"CID {row['clientId']} {row['label']}", fill=(255, 255, 255, 255))
    for target in target_placements:
        box = target["object"]
        draw.rectangle((box["x"], box["y"], box["x"] + box["width"] - 1, box["y"] + box["height"] - 1), outline=(255, 64, 64, 255), width=2)
    preview_path = args.out_dir / "gray-rubble-family-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "gray-rubble-family-atlas",
        "version": args.version,
        "atlasSize": {"width": atlas.width, "height": atlas.height},
        "expectedUpscale2x": {"width": atlas.width * 2, "height": atlas.height * 2},
        "finalScale": 2,
        "familyClientIds": sorted(FAMILY_CLIENT_IDS),
        "representatives": [dict(spec) for spec in REPRESENTATIVES],
        "targetSpriteIds": sorted({int(sprite_id) for cid in FAMILY_CLIENT_IDS for sprite_id in things[cid].unique_sprites}),
        "targetPlacements": target_placements,
        "input": str(input_path),
        "preview": str(preview_path),
        "notes": [
            "Shared sprite ids appear in more than one object context across this family.",
            "The atlas keeps separate target objects for both straight variants and both diagonal contexts.",
            "Each panel includes the same construction on the floor above and below, using the client Z projection.",
            "During recut, each sprite id can be selected from the most suitable target object instead of blindly taking the last duplicate.",
        ],
    }
    (args.out_dir / "_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (args.out_dir / "_LEIA-ME.txt").write_text(
        (
            "UPSCALE ONLY gray-rubble-family-atlas-input.png\n"
            f"Expected 2x output size: {atlas.width * 2}x{atlas.height * 2}.\n"
            "Do not upscale the preview image.\n"
        ),
        encoding="ascii",
    )

    print(
        json.dumps(
            {
                "outDir": str(args.out_dir),
                "input": str(input_path),
                "preview": str(preview_path),
                "manifest": str(args.out_dir / '_manifest.json'),
                "targetSpriteIds": manifest["targetSpriteIds"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
