#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

from build_wall_continuity_atlas import render_thing, target_index_for_pattern
from export_simple_hd_candidates import SprArchive
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a recut-safe multi-level wall facade atlas."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--client-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--segments", type=int, default=16)
    parser.add_argument("--levels", type=int, default=5)
    parser.add_argument("--panel-gap", type=int, default=32)
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
            "This facade implementation expects patternY=1, patternZ=1 and one frame"
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
    rendered_width = thing.width * tile_size
    rendered_height = thing.height * tile_size
    panel_width = (args.segments - 1) * tile_size + rendered_width
    panel_height = (args.levels - 1) * tile_size + rendered_height
    atlas_height = thing.pattern_x * panel_height + (thing.pattern_x - 1) * args.panel_gap
    atlas = Image.new("RGBA", (panel_width, atlas_height), (0, 0, 0, 0))
    target_level = args.levels // 2
    extraction: list[dict[str, int]] = []
    panels: list[dict[str, object]] = []

    for target_pattern in range(thing.pattern_x):
        panel_y = target_pattern * (panel_height + args.panel_gap)
        target_segment = target_index_for_pattern(
            args.segments,
            thing.pattern_x,
            target_pattern,
        )
        target_x = target_segment * tile_size
        target_y = panel_y + target_level * tile_size

        placements: list[dict[str, int]] = []
        for level in range(args.levels):
            for segment in range(args.segments):
                pattern = segment % thing.pattern_x
                x = segment * tile_size
                y = panel_y + level * tile_size
                atlas.alpha_composite(rendered_patterns[pattern], (x, y))
                placements.append(
                    {
                        "level": level,
                        "segmentIndex": segment,
                        "patternX": pattern,
                        "sourceX": x,
                        "sourceY": y,
                    }
                )

        # Preserve a clean, fully recoverable target while retaining four-way context.
        atlas.alpha_composite(rendered_patterns[target_pattern], (target_x, target_y))

        target_components: list[dict[str, int]] = []
        for component in pattern_components[target_pattern]:
            crop = {
                **component,
                "patternX": target_pattern,
                "targetLevel": target_level,
                "targetSegmentIndex": target_segment,
                "atlasSourceX": target_x + component["sourceX"],
                "atlasSourceY": target_y + component["sourceY"],
                "atlasSourceWidth": tile_size,
                "atlasSourceHeight": tile_size,
                "outputScale": 2,
                "atlasOutputX": (target_x + component["sourceX"]) * 2,
                "atlasOutputY": (target_y + component["sourceY"]) * 2,
                "atlasOutputWidth": tile_size * 2,
                "atlasOutputHeight": tile_size * 2,
            }
            extraction.append(crop)
            target_components.append(crop)

        panels.append(
            {
                "targetPatternX": target_pattern,
                "targetLevel": target_level,
                "targetSegmentIndex": target_segment,
                "targetSourceX": target_x,
                "targetSourceY": target_y,
                "placements": placements,
                "extraction": target_components,
            }
        )

    atlas_path = args.output / f"cid-{args.client_id}-multi-level-facade-atlas-input.png"
    atlas.save(atlas_path)

    preview = Image.new("RGBA", atlas.size, (28, 28, 28, 255))
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for panel in panels:
        x = int(panel["targetSourceX"])
        y = int(panel["targetSourceY"])
        draw.rectangle(
            (x, y, x + rendered_width - 1, y + rendered_height - 1),
            outline=(255, 40, 40, 255),
            width=1,
        )
        for component in panel["extraction"]:
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
    preview_path = args.output / f"cid-{args.client_id}-multi-level-facade-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "connected-horizontal-multi-level-wall-atlas",
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
        "panelGap": args.panel_gap,
        "targetRenderedLast": True,
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
