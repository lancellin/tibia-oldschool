#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from export_simple_hd_candidates import SprArchive
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


TILE_SIZE = 32
OUTPUT_SCALE = 2


def sprite_index(
    thing,
    frame: int,
    z: int,
    pattern_x: int,
    pattern_y: int,
    layer: int,
    width: int,
    height: int,
) -> int:
    index = frame
    index = index * thing.pattern_z + z
    index = index * thing.pattern_y + pattern_y
    index = index * thing.pattern_x + pattern_x
    index = index * thing.layers + layer
    index = index * thing.height + height
    index = index * thing.width + width
    return index


def render_thing(
    archive: SprArchive,
    thing,
    pattern_x: int = 0,
    pattern_y: int = 0,
) -> tuple[Image.Image, list[dict[str, int]]]:
    image = Image.new(
        "RGBA",
        (thing.width * TILE_SIZE, thing.height * TILE_SIZE),
        (0, 0, 0, 0),
    )
    components: list[dict[str, int]] = []
    for layer in range(thing.layers):
        for height in range(thing.height):
            for width in range(thing.width):
                index = sprite_index(
                    thing,
                    frame=0,
                    z=0,
                    pattern_x=pattern_x,
                    pattern_y=pattern_y,
                    layer=layer,
                    width=width,
                    height=height,
                )
                sprite_id = int(thing.sprites[index])
                destination_x = (thing.width - width - 1) * TILE_SIZE
                destination_y = (thing.height - height - 1) * TILE_SIZE
                if sprite_id:
                    sprite = archive.decode(sprite_id)
                    if sprite is None:
                        raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
                    image.alpha_composite(sprite, (destination_x, destination_y))
                components.append(
                    {
                        "spriteId": sprite_id,
                        "layer": layer,
                        "widthIndex": width,
                        "heightIndex": height,
                        "sourceX": destination_x,
                        "sourceY": destination_y,
                        "sourceWidth": TILE_SIZE,
                        "sourceHeight": TILE_SIZE,
                    }
                )
    return image, components


def render_patterns(archive: SprArchive, thing) -> dict[tuple[int, int], Image.Image]:
    return {
        (pattern_x, pattern_y): render_thing(
            archive,
            thing,
            pattern_x,
            pattern_y,
        )[0]
        for pattern_y in range(thing.pattern_y)
        for pattern_x in range(thing.pattern_x)
    }


def unique_target_components(
    archive: SprArchive,
    thing,
    client_id: int,
    role: str,
    excluded_sprite_ids: set[int] | None = None,
) -> list[dict[str, int | str]]:
    excluded = excluded_sprite_ids or set()
    targets: list[dict[str, int | str]] = []
    seen: set[int] = set()
    for pattern_y in range(thing.pattern_y):
        for pattern_x in range(thing.pattern_x):
            _, components = render_thing(archive, thing, pattern_x, pattern_y)
            for component in components:
                sprite_id = int(component["spriteId"])
                if not sprite_id or sprite_id in seen or sprite_id in excluded:
                    continue
                seen.add(sprite_id)
                targets.append(
                    {
                        **component,
                        "clientId": client_id,
                        "role": role,
                        "patternX": pattern_x,
                        "patternY": pattern_y,
                    }
                )
    return targets


def choose_matching_step(
    arm_length: int,
    junction_tile: int,
    pattern_count: int,
    target_pattern: int,
) -> int:
    candidates = [
        step
        for step in range(arm_length)
        if (junction_tile - arm_length + step) % pattern_count == target_pattern
    ]
    if not candidates:
        raise SystemExit(
            f"No arm position matches pattern {target_pattern}/{pattern_count}"
        )
    return min(candidates, key=lambda step: abs(step - (arm_length - 1) / 2))


def wall_entries(
    things: dict[int, object],
    images: dict[int, dict[tuple[int, int], Image.Image]],
    horizontal_cid: int,
    vertical_cid: int,
    corner_cid: int,
    arm_length: int,
    junction_tile: int,
    horizontal_override: tuple[int, int, int] | None = None,
    vertical_override: tuple[int, int, int] | None = None,
    corner_override: int | None = None,
    pole_override: int | None = None,
) -> list[tuple[int, int, Image.Image, int, int, int, str]]:
    horizontal = things[horizontal_cid]
    vertical = things[vertical_cid]
    entries: list[tuple[int, int, Image.Image, int, int, int, str]] = []

    if pole_override is not None:
        pole = things[pole_override]
        entries.append(
            (
                junction_tile,
                junction_tile,
                images[pole_override][(0, 0)],
                pole_override,
                0,
                0,
                "pole",
            )
        )
        return entries

    for step in range(arm_length):
        world_x = junction_tile - arm_length + step
        world_y = junction_tile
        pattern_x = world_x % horizontal.pattern_x
        cid = horizontal_cid
        if horizontal_override is not None and step == horizontal_override[0]:
            cid = horizontal_override[1]
            pattern_x = horizontal_override[2]
        entries.append(
            (
                world_x,
                world_y,
                images[cid][(pattern_x, 0)],
                cid,
                pattern_x,
                0,
                "horizontal",
            )
        )

    for step in range(arm_length):
        world_x = junction_tile
        world_y = junction_tile - arm_length + step
        pattern_y = world_y % vertical.pattern_y
        cid = vertical_cid
        if vertical_override is not None and step == vertical_override[0]:
            cid = vertical_override[1]
            pattern_y = vertical_override[2]
        entries.append(
            (
                world_x,
                world_y,
                images[cid][(0, pattern_y)],
                cid,
                0,
                pattern_y,
                "vertical",
            )
        )

    effective_corner = corner_override or corner_cid
    entries.append(
        (
            junction_tile,
            junction_tile,
            images[effective_corner][(0, 0)],
            effective_corner,
            0,
            0,
            "corner",
        )
    )
    return entries


def draw_entries(
    atlas: Image.Image,
    entries: list[tuple[int, int, Image.Image, int, int, int, str]],
    panel_x: int,
    panel_y: int,
    floor_offset: int,
) -> None:
    for world_x, world_y, image, *_ in sorted(
        entries,
        key=lambda row: (row[0] + row[1], row[0]),
    ):
        atlas.alpha_composite(
            image,
            (
                panel_x + world_x * TILE_SIZE + floor_offset,
                panel_y + world_y * TILE_SIZE + floor_offset,
            ),
        )


def target_anchor(
    target: dict[str, int | str],
    things: dict[int, object],
    arm_length: int,
    junction_tile: int,
) -> tuple[int, int, int]:
    role = str(target["role"])
    thing = things[int(target["clientId"])]
    if role == "horizontal":
        step = choose_matching_step(
            arm_length,
            junction_tile,
            thing.pattern_x,
            int(target["patternX"]),
        )
        return junction_tile - arm_length + step, junction_tile, step
    if role == "vertical":
        step = choose_matching_step(
            arm_length,
            junction_tile,
            thing.pattern_y,
            int(target["patternY"]),
        )
        return junction_tile, junction_tile - arm_length + step, step
    return junction_tile, junction_tile, -1


def draw_base_context(
    atlas: Image.Image,
    panel_x: int,
    panel_y: int,
    target: dict[str, int | str],
    things: dict[int, object],
    images: dict[int, dict[tuple[int, int], Image.Image]],
    ids: dict[str, int],
    arm_length: int,
    floors: int,
    target_floor: int,
    junction_tile: int,
) -> tuple[int, int, int]:
    role = str(target["role"])
    target_cid = int(target["clientId"])
    target_world_x, target_world_y, target_step = target_anchor(
        target,
        things,
        arm_length,
        junction_tile,
    )

    for floor in reversed(range(floors)):
        horizontal_override = None
        vertical_override = None
        corner_override = None
        pole_override = None
        if role == "pole":
            pole_override = target_cid
        elif floor == target_floor:
            if role == "horizontal":
                horizontal_override = (
                    target_step,
                    target_cid,
                    int(target["patternX"]),
                )
            elif role == "vertical":
                vertical_override = (
                    target_step,
                    target_cid,
                    int(target["patternY"]),
                )
            elif role == "corner":
                corner_override = target_cid

        entries = wall_entries(
            things,
            images,
            ids["horizontal"],
            ids["vertical"],
            ids["corner"],
            arm_length,
            junction_tile,
            horizontal_override=horizontal_override,
            vertical_override=vertical_override,
            corner_override=corner_override,
            pole_override=pole_override,
        )
        draw_entries(
            atlas,
            entries,
            panel_x,
            panel_y,
            floor * TILE_SIZE,
        )

    return (
        target_world_x,
        target_world_y,
        target_floor * TILE_SIZE,
    )


def draw_upper_z_railing_context(
    atlas: Image.Image,
    panel_x: int,
    panel_y: int,
    target: dict[str, int | str],
    things: dict[int, object],
    images: dict[int, dict[tuple[int, int], Image.Image]],
    ids: dict[str, int],
    arm_length: int,
    junction_tile: int,
) -> tuple[int, int, int]:
    target_world_x, target_world_y, target_step = target_anchor(
        target,
        things,
        arm_length,
        junction_tile,
    )

    # The stone wall is one Z below and therefore projects +32,+32 on screen.
    lower_entries = wall_entries(
        things,
        images,
        ids["horizontal"],
        ids["vertical"],
        ids["corner"],
        arm_length,
        junction_tile,
    )
    draw_entries(
        atlas,
        lower_entries,
        panel_x,
        panel_y,
        TILE_SIZE,
    )

    role = str(target["role"])
    target_cid = int(target["clientId"])
    horizontal_override = None
    vertical_override = None
    corner_override = None
    pole_override = None
    if role == "horizontal":
        horizontal_override = (
            target_step,
            target_cid,
            int(target["patternX"]),
        )
    elif role == "vertical":
        vertical_override = (
            target_step,
            target_cid,
            int(target["patternY"]),
        )
    elif role == "corner":
        corner_override = target_cid
    elif role == "pole":
        pole_override = target_cid

    upper_entries = wall_entries(
        things,
        images,
        ids["railingHorizontal"],
        ids["railingVertical"],
        ids["railingCorner"],
        arm_length,
        junction_tile,
        horizontal_override=horizontal_override,
        vertical_override=vertical_override,
        corner_override=corner_override,
        pole_override=pole_override,
    )
    draw_entries(atlas, upper_entries, panel_x, panel_y, 0)
    return target_world_x, target_world_y, 0


def build_page(
    output_dir: Path,
    page_number: int,
    page_name: str,
    targets: list[dict[str, int | str]],
    archive: SprArchive,
    things: dict[int, object],
    images: dict[int, dict[tuple[int, int], Image.Image]],
    ids: dict[str, int],
    source_sprites: dict[int, Image.Image],
    source_dir: Path,
    arm_length: int,
    floors: int,
    target_floor: int,
    columns: int,
    gap: int,
    upper_z_railing: bool = False,
) -> dict[str, object]:
    margin_tiles = 1
    junction_tile = margin_tiles + arm_length
    context_floors = 2 if upper_z_railing else floors
    panel_tiles = margin_tiles * 2 + arm_length + 2 + context_floors - 1
    panel_size = panel_tiles * TILE_SIZE
    rows = math.ceil(len(targets) / columns)
    atlas_width = columns * panel_size + (columns - 1) * gap
    atlas_height = rows * panel_size + (rows - 1) * gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    extraction: list[dict[str, int | str]] = []
    panels: list[dict[str, object]] = []

    for panel_index, target in enumerate(targets):
        panel_column = panel_index % columns
        panel_row = panel_index // columns
        panel_x = panel_column * (panel_size + gap)
        panel_y = panel_row * (panel_size + gap)

        if upper_z_railing:
            world_x, world_y, floor_offset = draw_upper_z_railing_context(
                atlas,
                panel_x,
                panel_y,
                target,
                things,
                images,
                ids,
                arm_length,
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
                arm_length,
                floors,
                target_floor,
                junction_tile,
            )

        target_thing_x = panel_x + world_x * TILE_SIZE + floor_offset
        target_thing_y = panel_y + world_y * TILE_SIZE + floor_offset
        target_x = target_thing_x + int(target["sourceX"])
        target_y = target_thing_y + int(target["sourceY"])
        sprite_id = int(target["spriteId"])
        atlas.alpha_composite(source_sprites[sprite_id], (target_x, target_y))

        crop = {
            **target,
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
        panels.append(
            {
                "panelIndex": panel_index,
                "targetSpriteId": sprite_id,
                "targetClientId": int(target["clientId"]),
                "targetRole": str(target["role"]),
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelSize": panel_size,
                "extraction": crop,
            }
        )

    prefix = f"{page_number:02d}-{page_name}"
    atlas_path = output_dir / f"{prefix}-atlas-input.png"
    preview_path = output_dir / f"{prefix}-atlas-preview.png"
    manifest_path = output_dir / f"{prefix}-manifest.json"
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for crop in extraction:
        x = int(crop["atlasSourceX"])
        y = int(crop["atlasSourceY"])
        draw.rectangle(
            (x, y, x + TILE_SIZE - 1, y + TILE_SIZE - 1),
            outline=(255, 210, 40, 255),
            width=1,
        )
        draw.text(
            (x + 2, y + 2),
            str(crop["spriteId"]),
            fill=(255, 255, 255, 255),
        )
    preview.save(preview_path)

    for target in targets:
        sprite_id = int(target["spriteId"])
        source_sprites[sprite_id].save(source_dir / f"{sprite_id}.png")

    page_manifest = {
        "page": page_number,
        "name": page_name,
        "upperZRailingContext": upper_z_railing,
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "panelGrid": {"columns": columns, "rows": rows},
        "panelSize": [panel_size, panel_size],
        "panelGap": gap,
        "targetComponentRenderedLast": True,
        "targets": len(targets),
        "targetSpriteIds": [int(target["spriteId"]) for target in targets],
        "panels": panels,
        "extraction": extraction,
    }
    manifest_path.write_text(
        json.dumps(page_manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    return page_manifest


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a complete, reusable multi-page atlas for one RME wall family."
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
        "horizontal": 1295,
        "vertical": 1294,
        "corner": 1298,
        "pole": 1296,
        "horizontalWindow": 1738,
        "verticalWindow": 1739,
        "railingHorizontal": 2162,
        "railingVertical": 2164,
        "railingPole": 2166,
        "railingCorner": 2168,
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

    source_sprites: dict[int, Image.Image] = {}
    for thing in things.values():
        for sprite_id in thing.unique_sprites:
            sprite_id = int(sprite_id)
            if not sprite_id or sprite_id in source_sprites:
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
    horizontal_sprite_ids = {
        int(target["spriteId"]) for target in horizontal_targets
    }
    horizontal_targets += unique_target_components(
        archive,
        things[ids["horizontalWindow"]],
        ids["horizontalWindow"],
        "horizontal",
        excluded_sprite_ids=horizontal_sprite_ids,
    )

    vertical_targets = unique_target_components(
        archive,
        things[ids["vertical"]],
        ids["vertical"],
        "vertical",
    )
    vertical_sprite_ids = {int(target["spriteId"]) for target in vertical_targets}
    vertical_targets += unique_target_components(
        archive,
        things[ids["verticalWindow"]],
        ids["verticalWindow"],
        "vertical",
        excluded_sprite_ids=vertical_sprite_ids,
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

    railing_horizontal_targets = unique_target_components(
        archive,
        things[ids["railingHorizontal"]],
        ids["railingHorizontal"],
        "horizontal",
    )
    used_railing_ids = {
        int(target["spriteId"]) for target in railing_horizontal_targets
    }
    railing_vertical_targets = unique_target_components(
        archive,
        things[ids["railingVertical"]],
        ids["railingVertical"],
        "vertical",
        excluded_sprite_ids=used_railing_ids,
    )
    used_railing_ids.update(
        int(target["spriteId"]) for target in railing_vertical_targets
    )
    railing_corner_targets = unique_target_components(
        archive,
        things[ids["railingCorner"]],
        ids["railingCorner"],
        "corner",
        excluded_sprite_ids=used_railing_ids,
    )
    used_railing_ids.update(
        int(target["spriteId"]) for target in railing_corner_targets
    )
    railing_pole_targets = unique_target_components(
        archive,
        things[ids["railingPole"]],
        ids["railingPole"],
        "pole",
        excluded_sprite_ids=used_railing_ids,
    )
    railing_targets = (
        railing_horizontal_targets
        + railing_vertical_targets
        + railing_corner_targets
        + railing_pole_targets
    )

    pages = [
        build_page(
            args.output,
            1,
            "horizontal-wall",
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
            "vertical-wall",
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
        build_page(
            args.output,
            4,
            "upper-z-stone-railing",
            railing_targets,
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
            upper_z_railing=True,
        ),
    ]

    all_target_ids = [
        sprite_id
        for page in pages
        for sprite_id in page["targetSpriteIds"]
    ]
    if len(all_target_ids) != len(set(all_target_ids)):
        raise SystemExit("A Sprite ID was assigned to more than one atlas page")

    coverage = {
        "horizontal": {
            "primaryClientId": 1295,
            "aliasClientIds": [1297, 1303],
            "windowClientId": 1738,
        },
        "vertical": {
            "primaryClientId": 1294,
            "aliasClientIds": [1299, 1301],
            "windowClientId": 1739,
        },
        "corner": {
            "primaryClientId": 1298,
            "aliasClientIds": [1300, 1302, 1304],
        },
        "pole": {"primaryClientId": 1296, "aliasClientIds": []},
        "upperZRailing": {
            "horizontalClientId": 2162,
            "verticalClientId": 2164,
            "poleClientId": 2166,
            "cornerClientId": 2168,
            "zRelation": "one floor above the stone wall",
            "screenProjection": "upper Z is -32,-32 relative to the wall below",
        },
    }
    manifest = {
        "type": "complete-rme-wall-family-atlas-set",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "coverage": coverage,
        "pages": pages,
        "uniqueTargetSpriteIds": sorted(all_target_ids),
        "uniqueTargetSpriteCount": len(all_target_ids),
        "recutRule": (
            "Crop each page at atlasOutput coordinates after 2x upscale, "
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
                "STONE WALL - COMPLETE ATLAS SET",
                "",
                "Upscale only the four files ending in -atlas-input.png.",
                "Expected scale: exactly 2x.",
                "",
                "Page 01: horizontal wall plus horizontal window.",
                "Page 02: vertical wall plus vertical window.",
                "Page 03: corners and pole.",
                "Page 04: stone railing on the Z immediately above the wall.",
                "",
                "CID 1299 is covered by the same Sprite IDs as CID 1294.",
                "The manifests preserve all crop coordinates and aliases.",
            ]
        ),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
