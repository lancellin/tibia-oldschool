#!/usr/bin/env python3
import argparse
import json
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


def render_thing(archive: SprArchive, thing, pattern_x: int) -> tuple[Image.Image, list[dict[str, int]]]:
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
                    pattern_y=0,
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


def target_index_for_pattern(segments: int, pattern_count: int, pattern: int) -> int:
    center = segments // 2
    candidates = [
        index
        for index in range(2, segments - 2)
        if index % pattern_count == pattern
    ]
    return min(candidates, key=lambda index: abs(index - center))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a recut-safe connected wall atlas from one horizontal DAT Client ID."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--client-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--segments", type=int, default=16)
    parser.add_argument("--row-gap", type=int, default=32)
    args = parser.parse_args()

    if args.segments < 8:
        raise SystemExit("--segments must be at least 8")

    thing = parse_dat(
        args.dat,
        772,
        THING_CATEGORY_ITEM,
        {args.client_id},
    ).get(args.client_id)
    if thing is None:
        raise SystemExit(f"Client ID {args.client_id} not found")
    if thing.pattern_y != 1 or thing.pattern_z != 1 or thing.frames != 1:
        raise SystemExit(
            "This first wall-atlas implementation expects patternY=1, patternZ=1 and one frame"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)

    rendered_patterns: dict[int, Image.Image] = {}
    pattern_components: dict[int, list[dict[str, int]]] = {}
    for pattern in range(thing.pattern_x):
        rendered, components = render_thing(archive, thing, pattern)
        rendered_patterns[pattern] = rendered
        pattern_components[pattern] = components
        rendered.save(args.output / f"cid-{args.client_id}-pattern-{pattern}-recomposed.png")
        for component in components:
            sprite_id = component["spriteId"]
            source_path = source_dir / f"{sprite_id}.png"
            if not source_path.exists():
                sprite = archive.decode(sprite_id)
                if sprite is None:
                    raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
                sprite.save(source_path)

    tile_size = 32
    strip_width = (args.segments - 1) * tile_size + thing.width * tile_size
    strip_height = thing.height * tile_size
    atlas_height = thing.pattern_x * strip_height + (thing.pattern_x - 1) * args.row_gap
    atlas = Image.new("RGBA", (strip_width, atlas_height), (0, 0, 0, 0))
    extraction: list[dict[str, int]] = []
    strip_rows: list[dict[str, object]] = []

    for target_pattern in range(thing.pattern_x):
        row_y = target_pattern * (strip_height + args.row_gap)
        target_index = target_index_for_pattern(
            args.segments,
            thing.pattern_x,
            target_pattern,
        )

        placements: list[dict[str, int]] = []
        for index in range(args.segments):
            pattern = index % thing.pattern_x
            x = index * tile_size
            atlas.alpha_composite(rendered_patterns[pattern], (x, row_y))
            placements.append(
                {
                    "segmentIndex": index,
                    "patternX": pattern,
                    "sourceX": x,
                    "sourceY": row_y,
                }
            )

        # Keep the extraction target above overlapping neighbors while retaining their context.
        target_x = target_index * tile_size
        atlas.alpha_composite(rendered_patterns[target_pattern], (target_x, row_y))

        target_components: list[dict[str, int]] = []
        for component in pattern_components[target_pattern]:
            crop = {
                **component,
                "patternX": target_pattern,
                "targetSegmentIndex": target_index,
                "atlasSourceX": target_x + component["sourceX"],
                "atlasSourceY": row_y + component["sourceY"],
                "atlasSourceWidth": tile_size,
                "atlasSourceHeight": tile_size,
                "outputScale": 2,
                "atlasOutputX": (target_x + component["sourceX"]) * 2,
                "atlasOutputY": (row_y + component["sourceY"]) * 2,
                "atlasOutputWidth": tile_size * 2,
                "atlasOutputHeight": tile_size * 2,
            }
            extraction.append(crop)
            target_components.append(crop)

        strip_rows.append(
            {
                "targetPatternX": target_pattern,
                "targetSegmentIndex": target_index,
                "targetSourceX": target_x,
                "targetSourceY": row_y,
                "placements": placements,
                "extraction": target_components,
            }
        )

    atlas_path = args.output / f"cid-{args.client_id}-wall-continuity-atlas-input.png"
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for row in strip_rows:
        x = int(row["targetSourceX"])
        y = int(row["targetSourceY"])
        draw.rectangle(
            (x, y, x + thing.width * tile_size - 1, y + thing.height * tile_size - 1),
            outline=(255, 40, 40, 255),
            width=1,
        )
        for component in row["extraction"]:
            cx = int(component["atlasSourceX"])
            cy = int(component["atlasSourceY"])
            draw.rectangle(
                (cx, cy, cx + tile_size - 1, cy + tile_size - 1),
                outline=(255, 210, 40, 255),
                width=1,
            )
            draw.text(
                (cx + 2, cy + 2),
                str(component["spriteId"]),
                fill=(255, 255, 255, 255),
            )
    preview_path = args.output / f"cid-{args.client_id}-wall-continuity-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "connected-horizontal-wall-atlas",
        "clientId": args.client_id,
        "datVersion": 772,
        "sourceDat": str(args.dat),
        "sourceSpr": str(args.spr),
        "thing": {
            "width": thing.width,
            "height": thing.height,
            "layers": thing.layers,
            "patternX": thing.pattern_x,
            "patternY": thing.pattern_y,
            "patternZ": thing.pattern_z,
            "frames": thing.frames,
            "orderedSpriteIds": thing.sprites,
            "uniqueSpriteIds": thing.unique_sprites,
        },
        "atlas": str(atlas_path),
        "preview": str(preview_path),
        "atlasSourceSize": [atlas.width, atlas.height],
        "expectedUpscaleSize2x": [atlas.width * 2, atlas.height * 2],
        "segmentsPerStrip": args.segments,
        "rowGap": args.row_gap,
        "targetRenderedLast": True,
        "recutRule": "Crop output coordinates from extraction, then restore the original Sprite ID alpha at 2x.",
        "strips": strip_rows,
        "extraction": extraction,
    }
    manifest_path = args.output / "_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
