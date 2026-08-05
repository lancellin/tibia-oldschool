#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from build_complete_wall_family_atlas import (
    OUTPUT_SCALE,
    TILE_SIZE,
    SprArchive,
    draw_base_context,
    render_patterns,
    render_thing,
)
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build one scene-based plaster wall atlas for geometry-preserving upscale."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arm-length", type=int, default=7)
    parser.add_argument("--floors", type=int, default=3)
    parser.add_argument("--target-floor", type=int, default=1)
    parser.add_argument("--columns", type=int, default=3)
    parser.add_argument("--gap", type=int, default=32)
    args = parser.parse_args()

    ids = {
        "horizontal": 1282,
        "vertical": 1281,
        "corner": 1289,
        "pole": 1283,
        "horizontalWindow": 1734,
        "verticalWindow": 1735,
    }
    things = parse_dat(
        args.dat,
        772,
        THING_CATEGORY_ITEM,
        set(ids.values()),
    )
    if set(things) != set(ids.values()):
        raise SystemExit(
            f"Missing Client IDs: {sorted(set(ids.values()) - set(things))}"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)
    images = {
        client_id: render_patterns(archive, thing)
        for client_id, thing in things.items()
    }

    scenes = [
        {
            "name": "horizontal-wall",
            "clientId": ids["horizontal"],
            "role": "horizontal",
            "includedSpriteIds": {53, 54, 55, 56},
        },
        {
            "name": "horizontal-window",
            "clientId": ids["horizontalWindow"],
            "role": "horizontal",
            "includedSpriteIds": {69, 70},
        },
        {
            "name": "vertical-wall",
            "clientId": ids["vertical"],
            "role": "vertical",
            "includedSpriteIds": {57, 58, 59, 60},
        },
        {
            "name": "vertical-window",
            "clientId": ids["verticalWindow"],
            "role": "vertical",
            "includedSpriteIds": {71, 72},
        },
        {
            "name": "corner",
            "clientId": ids["corner"],
            "role": "corner",
            "includedSpriteIds": {65, 66, 67, 68},
        },
        {
            "name": "pole",
            "clientId": ids["pole"],
            "role": "pole",
            "includedSpriteIds": {61, 62, 63, 64},
        },
    ]

    margin_tiles = 1
    junction_tile = margin_tiles + args.arm_length
    panel_tiles = margin_tiles * 2 + args.arm_length + 2 + args.floors - 1
    panel_size = panel_tiles * TILE_SIZE
    rows = math.ceil(len(scenes) / args.columns)
    atlas_width = args.columns * panel_size + (args.columns - 1) * args.gap
    atlas_height = rows * panel_size + (rows - 1) * args.gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    extraction: list[dict[str, int | str]] = []
    panels: list[dict[str, object]] = []
    all_target_ids: set[int] = set()

    for panel_index, scene in enumerate(scenes):
        client_id = int(scene["clientId"])
        role = str(scene["role"])
        thing = things[client_id]
        target_image, components = render_thing(archive, thing, 0, 0)
        included_ids = set(scene["includedSpriteIds"])

        panel_column = panel_index % args.columns
        panel_row = panel_index // args.columns
        panel_x = panel_column * (panel_size + args.gap)
        panel_y = panel_row * (panel_size + args.gap)

        representative = next(
            component
            for component in components
            if int(component["spriteId"]) in included_ids
        )
        target = {
            **representative,
            "clientId": client_id,
            "role": role,
            "patternX": 0,
            "patternY": 0,
        }
        world_x, world_y, floor_offset = draw_base_context(
            atlas,
            panel_x,
            panel_y,
            target,
            things,
            images,
            ids,
            args.arm_length,
            args.floors,
            args.target_floor,
            junction_tile,
        )
        target_thing_x = panel_x + world_x * TILE_SIZE + floor_offset
        target_thing_y = panel_y + world_y * TILE_SIZE + floor_offset

        # Redraw the complete 2x2 object as one unit. The upscaler therefore sees
        # every diagonal crossing before any component is cut back to 64x64.
        atlas.alpha_composite(target_image, (target_thing_x, target_thing_y))

        scene_extraction: list[dict[str, int | str]] = []
        for component in components:
            sprite_id = int(component["spriteId"])
            if sprite_id not in included_ids or sprite_id in all_target_ids:
                continue
            all_target_ids.add(sprite_id)
            sprite = archive.decode(sprite_id)
            if sprite is None:
                raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
            sprite.save(source_dir / f"{sprite_id}.png")

            target_x = target_thing_x + int(component["sourceX"])
            target_y = target_thing_y + int(component["sourceY"])
            crop = {
                **component,
                "clientId": client_id,
                "role": role,
                "scene": str(scene["name"]),
                "panelIndex": panel_index,
                "panelColumn": panel_column,
                "panelRow": panel_row,
                "atlasSourceX": target_x,
                "atlasSourceY": target_y,
                "atlasSourceWidth": TILE_SIZE,
                "atlasSourceHeight": TILE_SIZE,
                "outputScale": OUTPUT_SCALE,
                "atlasOutputX": target_x * OUTPUT_SCALE,
                "atlasOutputY": target_y * OUTPUT_SCALE,
                "atlasOutputWidth": TILE_SIZE * OUTPUT_SCALE,
                "atlasOutputHeight": TILE_SIZE * OUTPUT_SCALE,
            }
            extraction.append(crop)
            scene_extraction.append(crop)

        panels.append(
            {
                "panelIndex": panel_index,
                "scene": scene["name"],
                "targetClientId": client_id,
                "targetRole": role,
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelSize": panel_size,
                "targetThingSourceRect": [
                    target_thing_x,
                    target_thing_y,
                    thing.width * TILE_SIZE,
                    thing.height * TILE_SIZE,
                ],
                "targetThingOutputRect2x": [
                    target_thing_x * OUTPUT_SCALE,
                    target_thing_y * OUTPUT_SCALE,
                    thing.width * TILE_SIZE * OUTPUT_SCALE,
                    thing.height * TILE_SIZE * OUTPUT_SCALE,
                ],
                "extraction": scene_extraction,
            }
        )

    expected_ids = set(range(53, 73))
    if all_target_ids != expected_ids:
        raise SystemExit(
            f"Unexpected target coverage. Missing={sorted(expected_ids - all_target_ids)} "
            f"extra={sorted(all_target_ids - expected_ids)}"
        )

    atlas_path = args.output / "plaster-wall-high-fidelity-atlas-input.png"
    preview_path = args.output / "plaster-wall-high-fidelity-atlas-preview.png"
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for panel in panels:
        x, y, width, height = panel["targetThingSourceRect"]
        draw.rectangle(
            (x, y, x + width - 1, y + height - 1),
            outline=(60, 220, 255, 255),
            width=2,
        )
        draw.text(
            (int(panel["panelSourceX"]) + 4, int(panel["panelSourceY"]) + 4),
            f"{panel['scene']} CID {panel['targetClientId']}",
            fill=(255, 255, 255, 255),
        )
    for crop in extraction:
        x = int(crop["atlasSourceX"])
        y = int(crop["atlasSourceY"])
        draw.text((x + 2, y + 2), str(crop["spriteId"]), fill=(255, 210, 40, 255))
    preview.save(preview_path)

    manifest = {
        "type": "plaster-wall-high-fidelity-complete-object-atlas",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "panelGrid": {"columns": args.columns, "rows": rows},
        "panelSize": [panel_size, panel_size],
        "panelGap": args.gap,
        "completeTargetThingRenderedLast": True,
        "geometryRule": (
            "Each 2x2 Thing is upscaled as one 64x64 source block before its "
            "four 64x64 output components are extracted."
        ),
        "coverage": {
            "horizontal": {
                "primaryClientId": 1282,
                "aliasClientIds": [1284, 1290],
                "windowClientId": 1734,
            },
            "vertical": {
                "primaryClientId": 1281,
                "aliasClientIds": [1286, 1288],
                "windowClientId": 1735,
            },
            "corner": {
                "primaryClientId": 1289,
                "aliasClientIds": [1285, 1287, 1291, 1292, 1293],
            },
            "pole": {"primaryClientId": 1283},
            "excludedDoors": list(range(1628, 1650)),
            "preservedStoneWallSpriteIds": list(range(581, 589)),
        },
        "targetSpriteIds": sorted(all_target_ids),
        "targetSpriteCount": len(all_target_ids),
        "panels": panels,
        "extraction": extraction,
        "recutRule": (
            "Use the extraction output coordinates after exact 2x upscale and "
            "restore each original Sprite ID alpha at 2x."
        ),
    }
    (args.output / "_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (args.output / "_LEIA-ME.txt").write_text(
        "\n".join(
            [
                "PLASTER WALL - HIGH FIDELITY ATLAS",
                "",
                "Upscale only plaster-wall-high-fidelity-atlas-input.png.",
                "Use High Fidelity and exact 2x output.",
                "",
                "This atlas processes every 2x2 object as one complete block.",
                "Do not use the previous three per-component atlas inputs.",
                "Doors are excluded and Sprite IDs 581-588 remain untouched.",
            ]
        ),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
