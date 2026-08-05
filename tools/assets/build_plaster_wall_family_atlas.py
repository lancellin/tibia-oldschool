#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from build_complete_wall_family_atlas import (
    SprArchive,
    build_page,
    render_patterns,
    unique_target_components,
)
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build the complete structural atlas set for the 7.72 plaster wall."
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
        "horizontal": 1282,
        "vertical": 1281,
        "corner": 1289,
        "pole": 1283,
        "horizontalWindow": 1734,
        "verticalWindow": 1735,
    }
    all_client_ids = set(ids.values())
    things = parse_dat(
        args.dat,
        772,
        THING_CATEGORY_ITEM,
        all_client_ids,
    )
    if set(things) != all_client_ids:
        raise SystemExit(
            f"Missing Client IDs: {sorted(all_client_ids - set(things))}"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)
    images = {
        client_id: render_patterns(archive, thing)
        for client_id, thing in things.items()
    }

    source_sprites = {}
    for thing in things.values():
        for sprite_id in thing.unique_sprites:
            sprite_id = int(sprite_id)
            if sprite_id in source_sprites:
                continue
            sprite = archive.decode(sprite_id)
            if sprite is None:
                raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
            source_sprites[sprite_id] = sprite

    horizontal_targets = unique_target_components(
        archive,
        things[ids["horizontal"]],
        ids["horizontal"],
        "horizontal",
    )
    used_horizontal = {int(row["spriteId"]) for row in horizontal_targets}
    horizontal_targets += unique_target_components(
        archive,
        things[ids["horizontalWindow"]],
        ids["horizontalWindow"],
        "horizontal",
        excluded_sprite_ids=used_horizontal,
    )

    vertical_targets = unique_target_components(
        archive,
        things[ids["vertical"]],
        ids["vertical"],
        "vertical",
    )
    used_vertical = {int(row["spriteId"]) for row in vertical_targets}
    vertical_targets += unique_target_components(
        archive,
        things[ids["verticalWindow"]],
        ids["verticalWindow"],
        "vertical",
        excluded_sprite_ids=used_vertical,
    )

    corner_targets = unique_target_components(
        archive,
        things[ids["corner"]],
        ids["corner"],
        "corner",
    )
    corner_targets += unique_target_components(
        archive,
        things[ids["pole"]],
        ids["pole"],
        "pole",
    )

    pages = [
        build_page(
            args.output,
            1,
            "horizontal-wall-and-window",
            horizontal_targets,
            archive,
            things,
            images,
            ids,
            source_sprites,
            source_dir,
            args.arm_length,
            args.floors,
            args.target_floor,
            args.columns,
            args.gap,
        ),
        build_page(
            args.output,
            2,
            "vertical-wall-and-window",
            vertical_targets,
            archive,
            things,
            images,
            ids,
            source_sprites,
            source_dir,
            args.arm_length,
            args.floors,
            args.target_floor,
            args.columns,
            args.gap,
        ),
        build_page(
            args.output,
            3,
            "corners-and-pole",
            corner_targets,
            archive,
            things,
            images,
            ids,
            source_sprites,
            source_dir,
            args.arm_length,
            args.floors,
            args.target_floor,
            args.columns,
            args.gap,
        ),
    ]

    target_sprite_ids = [
        sprite_id
        for page in pages
        for sprite_id in page["targetSpriteIds"]
    ]
    if len(target_sprite_ids) != len(set(target_sprite_ids)):
        raise SystemExit("A Sprite ID was assigned to more than one page")

    manifest = {
        "type": "complete-plaster-wall-structural-atlas-set",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
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
            "pole": {
                "primaryClientId": 1283,
                "aliasClientIds": [],
            },
            "excludedDoors": list(range(1628, 1650)),
            "preservedStoneWallSpriteIds": list(range(581, 589)),
        },
        "pages": pages,
        "uniqueTargetSpriteIds": sorted(target_sprite_ids),
        "uniqueTargetSpriteCount": len(target_sprite_ids),
        "recutRule": (
            "Crop each page at atlasOutput coordinates after exact 2x upscale, "
            "then restore the matching original Sprite ID alpha at 2x."
        ),
    }
    (args.output / "_manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (args.output / "_LEIA-ME.txt").write_text(
        "\n".join(
            [
                "PLASTER WALL - COMPLETE STRUCTURAL ATLAS SET",
                "",
                "Upscale only the three files ending in -atlas-input.png.",
                "Expected scale: exactly 2x.",
                "",
                "Page 01: horizontal wall and horizontal window.",
                "Page 02: vertical wall and vertical window.",
                "Page 03: corners and pole.",
                "",
                "Doors are intentionally excluded.",
                "Sprite IDs 581-588 belong to the approved stone wall and are preserved.",
            ]
        ),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
