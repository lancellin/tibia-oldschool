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
    draw_upper_z_railing_context,
    render_patterns,
    render_thing,
)
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a complete-object atlas for the 7.72 Egypt stone wall family."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arm-length", type=int, default=7)
    parser.add_argument("--floors", type=int, default=3)
    parser.add_argument("--target-floor", type=int, default=1)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--gap", type=int, default=32)
    args = parser.parse_args()

    ids = {
        "horizontal": 1346,
        "vertical": 1345,
        "corner": 1349,
        "pole": 1347,
        "horizontalWindow": 1746,
        "verticalWindow": 1747,
        "railingHorizontal": 2201,
        "railingVertical": 2203,
        "railingPole": 2205,
        "railingCorner": 2207,
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
        ("horizontal-wall-pattern-0", 1346, "horizontal", 0, 0, {4724, 4725, 4726, 4727}, False),
        ("horizontal-wall-pattern-1", 1346, "horizontal", 1, 0, {4728, 4729, 4730, 4731}, False),
        ("horizontal-window", 1746, "horizontal", 0, 0, {6385, 6386}, False),
        ("vertical-wall-pattern-0", 1345, "vertical", 0, 0, {4712, 4713, 4714, 4715}, False),
        ("vertical-wall-pattern-1", 1345, "vertical", 0, 1, {4716, 4717, 4718, 4719}, False),
        ("vertical-window", 1747, "vertical", 0, 0, {6389, 6390}, False),
        ("corner", 1349, "corner", 0, 0, {4708, 4709, 4710, 4711}, False),
        ("pole", 1347, "pole", 0, 0, {4720, 4721, 4722, 4723}, False),
        ("railing-horizontal-pattern-0", 2201, "horizontal", 0, 0, {4930, 4931}, True),
        ("railing-horizontal-pattern-1", 2201, "horizontal", 1, 0, {4937, 4938}, True),
        ("railing-vertical-pattern-0", 2203, "vertical", 0, 0, {4939, 4940}, True),
        ("railing-vertical-pattern-1", 2203, "vertical", 0, 1, {4932, 4933}, True),
        ("railing-corner", 2207, "corner", 0, 0, {4936}, True),
        ("railing-pole", 2205, "pole", 0, 0, {4941}, True),
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
    target_ids: set[int] = set()

    for panel_index, scene in enumerate(scenes):
        name, client_id, role, pattern_x, pattern_y, included_ids, upper_z = scene
        thing = things[client_id]
        target_image, components = render_thing(
            archive,
            thing,
            pattern_x,
            pattern_y,
        )
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
            "patternX": pattern_x,
            "patternY": pattern_y,
        }
        if upper_z:
            world_x, world_y, floor_offset = draw_upper_z_railing_context(
                atlas,
                panel_x,
                panel_y,
                target,
                things,
                images,
                ids,
                args.arm_length,
                junction_tile,
            )
        else:
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

        target_x = panel_x + world_x * TILE_SIZE + floor_offset
        target_y = panel_y + world_y * TILE_SIZE + floor_offset
        atlas.alpha_composite(target_image, (target_x, target_y))

        scene_extraction = []
        for component in components:
            sprite_id = int(component["spriteId"])
            if not sprite_id or sprite_id not in included_ids or sprite_id in target_ids:
                continue
            target_ids.add(sprite_id)
            sprite = archive.decode(sprite_id)
            if sprite is None:
                raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
            sprite.save(source_dir / f"{sprite_id}.png")
            crop_x = target_x + int(component["sourceX"])
            crop_y = target_y + int(component["sourceY"])
            crop = {
                **component,
                "clientId": client_id,
                "role": role,
                "scene": name,
                "patternX": pattern_x,
                "patternY": pattern_y,
                "upperZ": upper_z,
                "panelIndex": panel_index,
                "atlasSourceX": crop_x,
                "atlasSourceY": crop_y,
                "atlasSourceWidth": TILE_SIZE,
                "atlasSourceHeight": TILE_SIZE,
                "outputScale": OUTPUT_SCALE,
                "atlasOutputX": crop_x * OUTPUT_SCALE,
                "atlasOutputY": crop_y * OUTPUT_SCALE,
                "atlasOutputWidth": TILE_SIZE * OUTPUT_SCALE,
                "atlasOutputHeight": TILE_SIZE * OUTPUT_SCALE,
            }
            extraction.append(crop)
            scene_extraction.append(crop)

        panels.append(
            {
                "panelIndex": panel_index,
                "scene": name,
                "targetClientId": client_id,
                "targetRole": role,
                "patternX": pattern_x,
                "patternY": pattern_y,
                "upperZ": upper_z,
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelSize": panel_size,
                "targetThingSourceRect": [
                    target_x,
                    target_y,
                    thing.width * TILE_SIZE,
                    thing.height * TILE_SIZE,
                ],
                "extraction": scene_extraction,
            }
        )

    expected_ids = (
        set(range(4708, 4732))
        | {4930, 4931, 4932, 4933, 4936, 4937, 4938, 4939, 4940, 4941}
        | {6385, 6386, 6389, 6390}
    )
    if target_ids != expected_ids:
        raise SystemExit(
            f"Unexpected coverage. Missing={sorted(expected_ids - target_ids)} "
            f"extra={sorted(target_ids - expected_ids)}"
        )

    atlas_path = args.output / "egypt-stone-wall-complete-atlas-input.png"
    preview_path = args.output / "egypt-stone-wall-complete-atlas-preview.png"
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
    preview.save(preview_path)

    manifest = {
        "type": "egypt-stone-wall-complete-object-atlas",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "completeTargetThingRenderedLast": True,
        "targetSpriteIds": sorted(target_ids),
        "targetSpriteCount": len(target_ids),
        "coverage": {
            "horizontal": {"primary": 1346, "aliases": [1348, 1354], "window": 1746},
            "vertical": {"primary": 1345, "aliases": [1350, 1352], "window": 1747},
            "corner": {"primary": 1349, "aliases": [1351, 1353, 1355]},
            "pole": 1347,
            "upperZRailing": {
                "horizontal": 2201,
                "vertical": 2203,
                "pole": 2205,
                "corner": 2207,
            },
        },
        "panels": panels,
        "extraction": extraction,
        "recutRule": (
            "Crop extraction coordinates after exact 2x upscale and restore "
            "each original Sprite ID alpha at 2x."
        ),
    }
    (args.output / "_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (args.output / "_LEIA-ME.txt").write_text(
        "\n".join(
            [
                "EGYPT STONE WALL - COMPLETE ATLAS",
                "",
                "Upscale only egypt-stone-wall-complete-atlas-input.png.",
                "Use exact 2x output.",
                "",
                "Includes wall CIDs 1345-1355, windows 1746/1747,",
                "and sandstone railing 2201/2203/2205/2207 on the upper Z.",
                "Every pattern is processed as a complete object before cutting.",
            ]
        ),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
