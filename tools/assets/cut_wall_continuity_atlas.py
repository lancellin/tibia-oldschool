#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Cut a 2x wall atlas using its manifest and restore each original component alpha."
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--upscaled", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    expected_size = tuple(manifest["expectedUpscaleSize2x"])
    with Image.open(args.upscaled) as image:
        atlas = image.convert("RGBA")
    if atlas.size != expected_size:
        raise SystemExit(f"Upscaled atlas is {atlas.size}, expected {expected_size}")

    if args.clean and args.output.exists():
        shutil.rmtree(args.output)
    sprites_dir = args.output / "sprites-64"
    sprites_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.manifest.parent / "source-components-32x"

    selected: list[dict[str, object]] = []
    by_pattern: dict[int, list[dict[str, int]]] = {}
    for row in manifest["extraction"]:
        sprite_id = int(row["spriteId"])
        pattern_x = int(row.get("patternX", 0))
        x = int(row["atlasOutputX"])
        y = int(row["atlasOutputY"])
        width = int(row["atlasOutputWidth"])
        height = int(row["atlasOutputHeight"])
        crop = atlas.crop((x, y, x + width, y + height))

        with Image.open(source_dir / f"{sprite_id}.png") as original_image:
            original = original_image.convert("RGBA")
        original_alpha = original.getchannel("A").resize(
            crop.size,
            Image.Resampling.LANCZOS,
        )
        raw_alpha = crop.getchannel("A").getextrema()
        crop.putalpha(original_alpha)
        output_path = sprites_dir / f"{sprite_id}.png"
        crop.save(output_path)

        selected.append(
            {
                "spriteId": sprite_id,
                "patternX": pattern_x,
                "sourceCrop": [x, y, width, height],
                "rawAlphaExtrema": list(raw_alpha),
                "restoredAlphaExtrema": list(original_alpha.getextrema()),
                "output": str(output_path),
            }
        )
        by_pattern.setdefault(pattern_x, []).append(row)

    thing = (
        manifest.get("thing")
        or manifest.get("cornerThing")
        or manifest.get("targetThing")
    )
    preview_path = None
    if thing is not None:
        preview_gap = 24
        rendered_width = int(thing["width"]) * 64
        rendered_height = int(thing["height"]) * 64
        preview = Image.new(
            "RGBA",
            (
                rendered_width * len(by_pattern)
                + preview_gap * (len(by_pattern) - 1),
                rendered_height + 22,
            ),
            (25, 25, 25, 255),
        )
        draw = ImageDraw.Draw(preview)
        for column, pattern in enumerate(sorted(by_pattern)):
            x_offset = column * (rendered_width + preview_gap)
            for row in by_pattern[pattern]:
                sprite_id = int(row["spriteId"])
                with Image.open(sprites_dir / f"{sprite_id}.png") as sprite_image:
                    sprite = sprite_image.convert("RGBA")
                destination_x = x_offset + int(row["sourceX"]) * 2
                destination_y = int(row["sourceY"]) * 2
                preview.alpha_composite(sprite, (destination_x, destination_y))
            draw.text(
                (x_offset + 2, rendered_height + 3),
                f"pattern {pattern}",
                fill=(235, 235, 235, 255),
            )
        preview_path = args.output / "recomposed-patterns-preview.png"
        preview.save(preview_path)

    summary = {
        "manifest": str(args.manifest),
        "upscaled": str(args.upscaled),
        "upscaledSize": list(atlas.size),
        "output": str(args.output),
        "spriteCount": len(selected),
        "preview": str(preview_path) if preview_path else None,
        "selected": selected,
    }
    (args.output / "cut-summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
