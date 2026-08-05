#!/usr/bin/env python3
import argparse
import csv
import json
import shutil
from pathlib import Path

from PIL import Image

from export_simple_hd_candidates import SprArchive


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate the general Upscayl batch and restore original SPR alpha masks at 2x."
    )
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--sprite-index", type=Path, required=True)
    parser.add_argument("--upscaled", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    with args.sprite_index.open(encoding="utf-8-sig") as input_file:
        expected_ids = {int(row["spriteId"]) for row in csv.DictReader(input_file)}
    input_files = {
        int(path.stem): path
        for path in args.upscaled.glob("*.png")
        if path.stem.isdigit()
    }
    missing = sorted(expected_ids - set(input_files))
    unexpected = sorted(set(input_files) - expected_ids)
    if missing or unexpected:
        raise SystemExit(
            f"Upscaled IDs do not match index; missing={missing}, unexpected={unexpected}"
        )

    if args.clean and args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True, exist_ok=True)

    archive = SprArchive(args.spr)
    raw_alpha_extrema: dict[str, int] = {}
    restored_alpha_extrema: dict[str, int] = {}
    for position, sprite_id in enumerate(sorted(expected_ids), start=1):
        with Image.open(input_files[sprite_id]) as image:
            upscaled = image.convert("RGBA")
        if upscaled.size != (64, 64):
            raise SystemExit(f"Sprite {sprite_id} is {upscaled.size}, expected 64x64")

        original = archive.decode(sprite_id)
        if original is None:
            raise SystemExit(f"Unable to decode original sprite {sprite_id}")
        original_alpha = original.getchannel("A").resize(
            (64, 64),
            Image.Resampling.LANCZOS,
        )

        raw_key = str(upscaled.getchannel("A").getextrema())
        restored_key = str(original_alpha.getextrema())
        raw_alpha_extrema[raw_key] = raw_alpha_extrema.get(raw_key, 0) + 1
        restored_alpha_extrema[restored_key] = restored_alpha_extrema.get(restored_key, 0) + 1

        upscaled.putalpha(original_alpha)
        upscaled.save(args.output / f"{sprite_id}.png")
        if position % 1000 == 0:
            print(f"[progress] {position}/{len(expected_ids)}")

    summary = {
        "spr": str(args.spr),
        "spriteIndex": str(args.sprite_index),
        "upscaled": str(args.upscaled),
        "output": str(args.output),
        "spriteCount": len(expected_ids),
        "rawAlphaExtrema": raw_alpha_extrema,
        "restoredAlphaExtrema": restored_alpha_extrema,
    }
    summary_path = args.output.parent / "prepare-summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
