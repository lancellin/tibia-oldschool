#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from export_simple_hd_candidates import SprArchive
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


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
    pattern_x: int,
    pattern_y: int,
) -> tuple[Image.Image, list[dict[str, int]]]:
    tile_size = 32
    image = Image.new(
        "RGBA",
        (thing.width * tile_size, thing.height * tile_size),
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
                sprite = archive.decode(sprite_id)
                if sprite is None:
                    raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
                destination_x = (thing.width - width - 1) * tile_size
                destination_y = (thing.height - height - 1) * tile_size
                image.alpha_composite(sprite, (destination_x, destination_y))
                components.append(
                    {
                        "spriteId": sprite_id,
                        "layer": layer,
                        "widthIndex": width,
                        "heightIndex": height,
                        "sourceX": destination_x,
                        "sourceY": destination_y,
                        "sourceWidth": tile_size,
                        "sourceHeight": tile_size,
                    }
                )
    return image, components


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build one L-corner context panel per corner component."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--horizontal-client-id", type=int, required=True)
    parser.add_argument("--vertical-client-id", type=int, required=True)
    parser.add_argument("--corner-client-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arm-length", type=int, default=9)
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--gap", type=int, default=32)
    args = parser.parse_args()

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
    if corner.pattern_x != 1 or corner.pattern_y != 1 or corner.frames != 1:
        raise SystemExit("Corner must have one pattern and one frame")

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)

    horizontal_images = {
        pattern_x: render_thing(archive, horizontal, pattern_x, 0)[0]
        for pattern_x in range(horizontal.pattern_x)
    }
    vertical_images = {
        pattern_y: render_thing(archive, vertical, 0, pattern_y)[0]
        for pattern_y in range(vertical.pattern_y)
    }
    corner_image, corner_components = render_thing(archive, corner, 0, 0)
    corner_image.save(args.output / f"cid-{args.corner_client_id}-recomposed.png")

    source_sprites: dict[int, Image.Image] = {}
    for component in corner_components:
        sprite_id = component["spriteId"]
        sprite = archive.decode(sprite_id)
        if sprite is None:
            raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
        source_sprites[sprite_id] = sprite
        sprite.save(source_dir / f"{sprite_id}.png")

    tile_size = 32
    margin_tiles = 1
    junction_tile_x = margin_tiles + args.arm_length
    junction_tile_y = margin_tiles + args.arm_length
    panel_tiles = margin_tiles * 2 + args.arm_length + 2
    panel_size = panel_tiles * tile_size
    rows = math.ceil(len(corner_components) / args.columns)
    atlas_width = args.columns * panel_size + (args.columns - 1) * args.gap
    atlas_height = rows * panel_size + (rows - 1) * args.gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    panels: list[dict[str, object]] = []
    extraction: list[dict[str, int]] = []

    for panel_index, component in enumerate(corner_components):
        panel_column = panel_index % args.columns
        panel_row = panel_index // args.columns
        panel_x = panel_column * (panel_size + args.gap)
        panel_y = panel_row * (panel_size + args.gap)

        # Horizontal arm approaches the junction from the left.
        for step in range(args.arm_length):
            anchor_tile_x = junction_tile_x - args.arm_length + step
            pattern_x = anchor_tile_x % horizontal.pattern_x
            atlas.alpha_composite(
                horizontal_images[pattern_x],
                (panel_x + anchor_tile_x * tile_size, panel_y + junction_tile_y * tile_size),
            )

        # Vertical arm approaches the junction from above.
        for step in range(args.arm_length):
            anchor_tile_y = junction_tile_y - args.arm_length + step
            pattern_y = anchor_tile_y % vertical.pattern_y
            atlas.alpha_composite(
                vertical_images[pattern_y],
                (panel_x + junction_tile_x * tile_size, panel_y + anchor_tile_y * tile_size),
            )

        corner_x = panel_x + junction_tile_x * tile_size
        corner_y = panel_y + junction_tile_y * tile_size
        atlas.alpha_composite(corner_image, (corner_x, corner_y))

        target_x = corner_x + int(component["sourceX"])
        target_y = corner_y + int(component["sourceY"])
        sprite_id = int(component["spriteId"])
        atlas.alpha_composite(source_sprites[sprite_id], (target_x, target_y))

        crop = {
            **component,
            "panelIndex": panel_index,
            "panelColumn": panel_column,
            "panelRow": panel_row,
            "horizontalClientId": args.horizontal_client_id,
            "verticalClientId": args.vertical_client_id,
            "cornerClientId": args.corner_client_id,
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
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelSize": panel_size,
                "junctionTile": [junction_tile_x, junction_tile_y],
                "extraction": crop,
            }
        )

    atlas_path = args.output / f"cid-{args.corner_client_id}-stone-wall-corner-context-atlas-input.png"
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
    preview_path = args.output / f"cid-{args.corner_client_id}-stone-wall-corner-context-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "stone-wall-l-corner-component-context-atlas",
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "horizontalClientId": args.horizontal_client_id,
        "verticalClientId": args.vertical_client_id,
        "cornerClientId": args.corner_client_id,
        "cornerThing": {
            "width": corner.width,
            "height": corner.height,
            "layers": corner.layers,
            "patternX": corner.pattern_x,
            "patternY": corner.pattern_y,
            "patternZ": corner.pattern_z,
            "frames": corner.frames,
            "orderedSpriteIds": corner.sprites,
            "uniqueSpriteIds": corner.unique_sprites,
        },
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "armLengthTiles": args.arm_length,
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
