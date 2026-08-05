#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from build_wall_continuity_atlas import render_thing, target_index_for_pattern
from export_simple_hd_candidates import SprArchive
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build one natural multi-level facade context panel per wall component."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--client-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--segments", type=int, default=12)
    parser.add_argument("--levels", type=int, default=5)
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--gap", type=int, default=32)
    args = parser.parse_args()

    if args.segments < 8:
        raise SystemExit("--segments must be at least 8")
    if args.levels < 3 or args.levels % 2 == 0:
        raise SystemExit("--levels must be an odd number of at least 3")

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
            "This component-context implementation expects patternY=1, patternZ=1 and one frame"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "source-components-32x"
    source_dir.mkdir(parents=True, exist_ok=True)
    archive = SprArchive(args.spr)

    rendered_patterns: dict[int, Image.Image] = {}
    pattern_components: dict[int, list[dict[str, int]]] = {}
    source_sprites: dict[int, Image.Image] = {}
    component_targets: list[dict[str, int]] = []
    for pattern in range(thing.pattern_x):
        rendered, components = render_thing(archive, thing, pattern)
        rendered_patterns[pattern] = rendered
        pattern_components[pattern] = components
        rendered.save(args.output / f"cid-{args.client_id}-pattern-{pattern}-recomposed.png")
        for component in components:
            sprite_id = component["spriteId"]
            sprite = archive.decode(sprite_id)
            if sprite is None:
                raise SystemExit(f"Unable to decode Sprite ID {sprite_id}")
            source_sprites[sprite_id] = sprite
            source_path = source_dir / f"{sprite_id}.png"
            if not source_path.exists():
                sprite.save(source_path)
            component_targets.append({**component, "patternX": pattern})

    tile_size = 32
    rendered_width = thing.width * tile_size
    rendered_height = thing.height * tile_size
    panel_width = (args.segments - 1) * tile_size + rendered_width
    panel_height = (args.levels - 1) * tile_size + rendered_height
    rows = math.ceil(len(component_targets) / args.columns)
    atlas_width = args.columns * panel_width + (args.columns - 1) * args.gap
    atlas_height = rows * panel_height + (rows - 1) * args.gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    target_level = args.levels // 2
    extraction: list[dict[str, int]] = []
    panels: list[dict[str, object]] = []

    for panel_index, component in enumerate(component_targets):
        panel_column = panel_index % args.columns
        panel_row = panel_index // args.columns
        panel_x = panel_column * (panel_width + args.gap)
        panel_y = panel_row * (panel_height + args.gap)
        target_pattern = int(component["patternX"])
        target_segment = target_index_for_pattern(
            args.segments,
            thing.pattern_x,
            target_pattern,
        )
        target_thing_x = panel_x + target_segment * tile_size
        target_thing_y = panel_y + target_level * tile_size

        for level in range(args.levels):
            for segment in range(args.segments):
                pattern = segment % thing.pattern_x
                x = panel_x + segment * tile_size
                y = panel_y + level * tile_size
                atlas.alpha_composite(rendered_patterns[pattern], (x, y))

        target_x = target_thing_x + int(component["sourceX"])
        target_y = target_thing_y + int(component["sourceY"])
        sprite_id = int(component["spriteId"])

        # Only the component being extracted is redrawn. The facade stays naturally connected.
        atlas.alpha_composite(source_sprites[sprite_id], (target_x, target_y))

        crop = {
            **component,
            "patternX": target_pattern,
            "targetLevel": target_level,
            "targetSegmentIndex": target_segment,
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
                "targetLevel": target_level,
                "targetSegmentIndex": target_segment,
                "panelSourceX": panel_x,
                "panelSourceY": panel_y,
                "panelWidth": panel_width,
                "panelHeight": panel_height,
                "extraction": crop,
            }
        )

    atlas_path = args.output / f"cid-{args.client_id}-component-context-atlas-input.png"
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for panel in panels:
        crop = panel["extraction"]
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
    preview_path = args.output / f"cid-{args.client_id}-component-context-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "multi-level-wall-component-context-atlas",
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
        "segmentsPerLevel": args.segments,
        "levelsPerPanel": args.levels,
        "panelGrid": {"columns": args.columns, "rows": rows},
        "panelSize": [panel_width, panel_height],
        "panelGap": args.gap,
        "targetComponentRenderedLast": True,
        "recutRule": "Crop output coordinates from extraction, then restore the original Sprite ID alpha at 2x.",
        "panels": panels,
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
