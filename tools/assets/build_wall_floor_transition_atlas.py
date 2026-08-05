#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from build_wall_corner_context_atlas import render_thing
from export_simple_hd_candidates import SprArchive
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build one exact three-floor L-wall context panel per target wall component."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--horizontal-client-id", type=int, required=True)
    parser.add_argument("--vertical-client-id", type=int, required=True)
    parser.add_argument("--corner-client-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arm-length", type=int, default=7)
    parser.add_argument("--floors", type=int, default=3)
    parser.add_argument("--target-floor", type=int, default=1)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--gap", type=int, default=32)
    args = parser.parse_args()

    if args.floors < 3:
        raise SystemExit("--floors must be at least 3")
    if not 0 <= args.target_floor < args.floors:
        raise SystemExit("--target-floor is outside the floor range")

    target_ids = {
        args.horizontal_client_id,
        args.vertical_client_id,
        args.corner_client_id,
    }
    things = parse_dat(
        args.dat,
        772,
        THING_CATEGORY_ITEM,
        target_ids,
    )
    if set(things) != target_ids:
        raise SystemExit(f"Missing Client IDs: {sorted(target_ids - set(things))}")
    horizontal = things[args.horizontal_client_id]
    vertical = things[args.vertical_client_id]
    corner = things[args.corner_client_id]

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)

    horizontal_images: dict[int, Image.Image] = {}
    horizontal_components: dict[int, list[dict[str, int]]] = {}
    for pattern_x in range(horizontal.pattern_x):
        image, components = render_thing(archive, horizontal, pattern_x, 0)
        horizontal_images[pattern_x] = image
        horizontal_components[pattern_x] = components

    vertical_images = {
        pattern_y: render_thing(archive, vertical, 0, pattern_y)[0]
        for pattern_y in range(vertical.pattern_y)
    }
    corner_image = render_thing(archive, corner, 0, 0)[0]

    target_components: list[dict[str, int]] = []
    source_sprites: dict[int, Image.Image] = {}
    for pattern_x in range(horizontal.pattern_x):
        for component in horizontal_components[pattern_x]:
            sprite_id = int(component["spriteId"])
            sprite = archive.decode(sprite_id)
            if sprite is None:
                raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
            source_sprites[sprite_id] = sprite
            sprite.save(source_dir / f"{sprite_id}.png")
            target_components.append({**component, "patternX": pattern_x})

    tile_size = 32
    rendered_size = 64
    margin_tiles = 1
    top_junction_tile = margin_tiles + args.arm_length
    floor_shift = tile_size
    panel_size = (
        (margin_tiles * 2 + args.arm_length + 2) * tile_size
        + (args.floors - 1) * floor_shift
    )
    rows = math.ceil(len(target_components) / args.columns)
    atlas_width = args.columns * panel_size + (args.columns - 1) * args.gap
    atlas_height = rows * panel_size + (rows - 1) * args.gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    panels: list[dict[str, object]] = []
    extraction: list[dict[str, int]] = []

    for panel_index, target_component in enumerate(target_components):
        panel_column = panel_index % args.columns
        panel_row = panel_index // args.columns
        panel_x = panel_column * (panel_size + args.gap)
        panel_y = panel_row * (panel_size + args.gap)
        target_pattern = int(target_component["patternX"])

        matching_segments = [
            step
            for step in range(args.arm_length)
            if (top_junction_tile - args.arm_length + step) % horizontal.pattern_x
            == target_pattern
        ]
        target_step = min(
            matching_segments,
            key=lambda step: abs(step - (args.arm_length - 1) / 2),
        )

        # The client draws lower floors first and higher floors last.
        for floor in reversed(range(args.floors)):
            floor_offset = floor * floor_shift
            junction_tile_x = top_junction_tile
            junction_tile_y = top_junction_tile
            floor_entries: list[tuple[int, int, Image.Image, str, int]] = []

            for step in range(args.arm_length):
                world_x = junction_tile_x - args.arm_length + step
                world_y = junction_tile_y
                pattern_x = world_x % horizontal.pattern_x
                floor_entries.append(
                    (
                        world_x,
                        world_y,
                        horizontal_images[pattern_x],
                        "horizontal",
                        pattern_x,
                    )
                )

            for step in range(args.arm_length):
                world_x = junction_tile_x
                world_y = junction_tile_y - args.arm_length + step
                pattern_y = world_y % vertical.pattern_y
                floor_entries.append(
                    (
                        world_x,
                        world_y,
                        vertical_images[pattern_y],
                        "vertical",
                        pattern_y,
                    )
                )

            floor_entries.append(
                (
                    junction_tile_x,
                    junction_tile_y,
                    corner_image,
                    "corner",
                    0,
                )
            )
            floor_entries.sort(key=lambda row: (row[0] + row[1], row[0]))

            for world_x, world_y, image, _role, _pattern in floor_entries:
                screen_x = panel_x + world_x * tile_size + floor_offset
                screen_y = panel_y + world_y * tile_size + floor_offset
                atlas.alpha_composite(image, (screen_x, screen_y))

        target_world_x = top_junction_tile - args.arm_length + target_step
        target_world_y = top_junction_tile
        target_floor_offset = args.target_floor * floor_shift
        target_thing_x = panel_x + target_world_x * tile_size + target_floor_offset
        target_thing_y = panel_y + target_world_y * tile_size + target_floor_offset
        target_x = target_thing_x + int(target_component["sourceX"])
        target_y = target_thing_y + int(target_component["sourceY"])
        sprite_id = int(target_component["spriteId"])

        # Preserve the complete target component while keeping exact Z-projected context.
        atlas.alpha_composite(source_sprites[sprite_id], (target_x, target_y))

        crop = {
            **target_component,
            "targetFloor": args.target_floor,
            "targetHorizontalStep": target_step,
            "panelIndex": panel_index,
            "panelColumn": panel_column,
            "panelRow": panel_row,
            "atlasSourceX": target_x,
            "atlasSourceY": target_y,
            "atlasSourceWidth": tile_size,
            "atlasSourceHeight": tile_size,
            "outputScale": 2,
            "atlasOutputX": target_x * 2,
            "atlasOutputY": target_y * 2,
            "atlasOutputWidth": tile_size * 2,
            "atlasOutputHeight": tile_size * 2,
        }
        extraction.append(crop)
        panels.append(
            {
                "panelIndex": panel_index,
                "targetSpriteId": sprite_id,
                "targetPatternX": target_pattern,
                "targetFloor": args.target_floor,
                "targetHorizontalStep": target_step,
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelSize": panel_size,
                "floorScreenOffsets": [
                    [floor * floor_shift, floor * floor_shift]
                    for floor in range(args.floors)
                ],
                "drawFloorOrder": list(reversed(range(args.floors))),
                "extraction": crop,
            }
        )

    atlas_path = args.output / (
        f"cid-{args.horizontal_client_id}-three-floor-transition-atlas-input.png"
    )
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for crop in extraction:
        x = int(crop["atlasSourceX"])
        y = int(crop["atlasSourceY"])
        draw.rectangle(
            (x, y, x + tile_size - 1, y + tile_size - 1),
            outline=(255, 210, 40, 255),
            width=1,
        )
        draw.text(
            (x + 2, y + 2),
            str(crop["spriteId"]),
            fill=(255, 255, 255, 255),
        )
    preview_path = args.output / (
        f"cid-{args.horizontal_client_id}-three-floor-transition-atlas-preview.png"
    )
    preview.save(preview_path)

    manifest = {
        "type": "three-floor-z-projected-wall-transition-atlas",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "horizontalClientId": args.horizontal_client_id,
        "verticalClientId": args.vertical_client_id,
        "cornerClientId": args.corner_client_id,
        "targetThing": {
            "width": horizontal.width,
            "height": horizontal.height,
            "layers": horizontal.layers,
            "patternX": horizontal.pattern_x,
            "patternY": horizontal.pattern_y,
            "patternZ": horizontal.pattern_z,
            "frames": horizontal.frames,
            "orderedSpriteIds": horizontal.sprites,
            "uniqueSpriteIds": horizontal.unique_sprites,
        },
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "armLengthTiles": args.arm_length,
        "floors": args.floors,
        "targetFloor": args.target_floor,
        "floorProjectionPerZ": [tile_size, tile_size],
        "drawFloorOrder": list(reversed(range(args.floors))),
        "panelGrid": {"columns": args.columns, "rows": rows},
        "panelSize": [panel_size, panel_size],
        "panelGap": args.gap,
        "targetComponentRenderedLast": True,
        "recutRule": "Crop output coordinates from extraction, then restore the original Sprite ID alpha at 2x.",
        "panels": panels,
        "extraction": extraction,
    }
    (args.output / "_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
