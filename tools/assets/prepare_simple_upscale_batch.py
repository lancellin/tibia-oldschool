#!/usr/bin/env python3
import argparse
import csv
import json
from pathlib import Path

from PIL import Image

from extract_sprites import decode_sprite


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate an individual Upscayl batch and restore each sprite's original alpha mask."
    )
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--sprite-index", type=Path, required=True)
    parser.add_argument("--upscaled", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rows = list(csv.DictReader(args.sprite_index.open(encoding="utf-8-sig")))
    expected_ids = {
        int(row["spriteId"])
        for row in rows
        if row["status"] == "para_upscale"
    }
    input_files = {
        int(path.stem): path
        for path in args.upscaled.glob("*.png")
        if path.stem.isdigit()
    }
    if set(input_files) != expected_ids:
        raise SystemExit(
            f"Upscaled IDs do not match index; missing={sorted(expected_ids - set(input_files))}, "
            f"unexpected={sorted(set(input_files) - expected_ids)}"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    selected: list[dict[str, object]] = []
    for sprite_id in sorted(expected_ids):
        with Image.open(input_files[sprite_id]) as image:
            upscaled = image.convert("RGBA")
        if upscaled.size != (64, 64):
            raise SystemExit(f"Sprite {sprite_id} is {upscaled.size}, expected 64x64")

        original = decode_sprite(args.spr, sprite_id, 32, False, False)
        if original is None:
            raise SystemExit(f"Unable to decode original sprite {sprite_id}")
        original_alpha = (
            original.convert("RGBA")
            .getchannel("A")
            .resize((64, 64), Image.Resampling.LANCZOS)
        )
        before_alpha = upscaled.getchannel("A").getextrema()
        upscaled.putalpha(original_alpha)
        output_path = args.output / f"{sprite_id}.png"
        upscaled.save(output_path)
        selected.append(
            {
                "spriteId": sprite_id,
                "source": str(input_files[sprite_id]),
                "output": str(output_path),
                "upscaledAlphaExtrema": list(before_alpha),
                "restoredAlphaExtrema": list(original_alpha.getextrema()),
            }
        )

    summary_path = args.output.parent / "prepare-summary.json"
    summary_path.write_text(
        json.dumps(
            {
                "spr": str(args.spr),
                "spriteIndex": str(args.sprite_index),
                "upscaled": str(args.upscaled),
                "output": str(args.output),
                "spriteCount": len(selected),
                "selected": selected,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "sprites": len(selected),
                "output": str(args.output),
                "summary": str(summary_path),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
