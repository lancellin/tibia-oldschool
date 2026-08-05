#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from merge_cwm import read_cwm
from patch_classic_spr_cross_version import read_spr, write_spr
from patch_rebased_water_borders import encode_sprite


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Downscale one client's CWM overrides into its existing classic SPR slots."
    )
    parser.add_argument("--client-id", type=int, required=True)
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--cwm", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    thing = parse_dat(
        args.dat,
        version=772,
        category=THING_CATEGORY_ITEM,
        target_ids={args.client_id},
    ).get(args.client_id)
    if thing is None:
        raise ValueError(f"Client ID {args.client_id} is missing from DAT")

    signature, sprite_count, sprites = read_spr(args.spr)
    _version, cwm_count, overrides = read_cwm(args.cwm)
    target_ids = thing.unique_sprites
    missing = [sprite_id for sprite_id in target_ids if sprite_id not in overrides]
    if missing:
        raise ValueError(f"Missing CWM overrides for sprite IDs: {missing}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    downscaled_dir = args.out_dir / "sprites-32"
    downscaled_dir.mkdir(parents=True, exist_ok=True)
    patched = dict(sprites)
    rows = []

    for sprite_id in target_ids:
        hd_path = args.out_dir / f"_cwm-{sprite_id}.png"
        hd_path.write_bytes(overrides[sprite_id])
        with Image.open(hd_path) as image:
            hd = image.convert("RGBA")
        hd_path.unlink()
        if hd.size != (64, 64):
            raise ValueError(f"CWM sprite {sprite_id} expected 64x64, got {hd.size}")

        classic = decode_sprite(args.spr, sprite_id, 32, False, False)
        if classic is None:
            raise ValueError(f"Unable to decode classic sprite {sprite_id}")
        reduced = hd.resize((32, 32), Image.Resampling.LANCZOS)
        reduced.save(downscaled_dir / f"{sprite_id}.png")
        patched[sprite_id] = encode_sprite(reduced)
        rows.append((sprite_id, classic.convert("RGBA"), reduced))

    write_spr(args.output, signature, sprite_count, patched)
    output_signature, output_count, output_sprites = read_spr(args.output)
    if output_signature != signature or output_count != sprite_count:
        raise ValueError("Output SPR signature or sprite count changed")

    changed_ids = sorted(
        sprite_id
        for sprite_id in range(1, sprite_count + 1)
        if sprites.get(sprite_id) != output_sprites.get(sprite_id)
    )
    if changed_ids != sorted(target_ids):
        raise ValueError(
            f"Changed slots differ from expected: changed={changed_ids}, expected={target_ids}"
        )

    sheet = Image.new("RGBA", (256, len(rows) * 144), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (sprite_id, classic, reduced) in enumerate(rows):
        y = index * 144
        sheet.alpha_composite(
            classic.resize((128, 128), Image.Resampling.NEAREST),
            (0, y + 16),
        )
        sheet.alpha_composite(
            reduced.resize((128, 128), Image.Resampling.NEAREST),
            (128, y + 16),
        )
        draw.text((4, y + 1), f"{sprite_id} classic", fill=(240, 240, 240, 255))
        draw.text((132, y + 1), f"{sprite_id} HD->SPR", fill=(240, 240, 240, 255))
    sheet_path = args.out_dir / "comparison.png"
    sheet.save(sheet_path)

    summary = {
        "clientId": args.client_id,
        "dat": str(args.dat),
        "sourceSpr": str(args.spr),
        "sourceCwm": str(args.cwm),
        "outputSpr": str(args.output),
        "sprSignature": signature.hex(),
        "sprSpriteCount": sprite_count,
        "cwmSpriteCount": cwm_count,
        "changedSpriteIds": changed_ids,
        "comparison": str(sheet_path),
    }
    summary_path = args.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
