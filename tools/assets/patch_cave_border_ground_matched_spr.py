#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw

from build_square_border_cutout_mosaic import keep_light_border
from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr
from patch_rebased_water_borders import encode_sprite


BORDER_CLIENT_IDS = tuple(range(4785, 4797))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Match cave-border sprite interiors to CID 351 while preserving the light rim."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--ground-client-id", type=int, default=351)
    parser.add_argument("--luminance", type=float, default=82)
    parser.add_argument("--saturation", type=float, default=0.64)
    args = parser.parse_args()

    wanted = {args.ground_client_id, *BORDER_CLIENT_IDS}
    things = parse_dat(args.dat, 772, THING_CATEGORY_ITEM, wanted)
    missing = sorted(wanted - set(things))
    if missing:
        raise ValueError(f"Missing client IDs in DAT: {missing}")

    ground_sprite_id = things[args.ground_client_id].unique_sprites[0]
    ground = decode_sprite(args.spr, ground_sprite_id, 32, False, False)
    if ground is None:
        raise ValueError(f"Unable to decode ground sprite {ground_sprite_id}")
    ground = ground.convert("RGBA")

    signature, sprite_count, sprites = read_spr(args.spr)
    patched = dict(sprites)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    sprites_dir = args.out_dir / "sprites"
    sprites_dir.mkdir(parents=True, exist_ok=True)
    entries = []

    for client_id in BORDER_CLIENT_IDS:
        target_ids = things[client_id].unique_sprites
        if len(target_ids) != 1:
            raise ValueError(f"CID {client_id} should reference exactly one sprite slot")
        target_id = target_ids[0]

        original = decode_sprite(args.spr, target_id, 32, False, False)
        if original is None:
            raise ValueError(f"Unable to decode target sprite {target_id}")
        original = original.convert("RGBA")
        original_alpha = original.getchannel("A")
        light = keep_light_border(original, args.luminance, args.saturation)

        treated = ground.copy()
        treated.putalpha(original_alpha)
        treated.alpha_composite(light)
        treated.save(sprites_dir / f"{target_id}.png")

        encoded = encode_sprite(treated)
        patched[target_id] = encoded
        entries.append(
            {
                "clientId": client_id,
                "targetSpriteId": target_id,
                "originalBlockSha256": sha256(sprites[target_id]),
                "treatedBlockSha256": sha256(encoded),
                "opaquePixels": sum(1 for value in original_alpha.getdata() if value),
                "lightPixels": sum(1 for value in light.getchannel("A").getdata() if value),
            }
        )

    write_spr(args.output, signature, sprite_count, patched)

    output_signature, output_count, output_sprites = read_spr(args.output)
    if output_signature != signature or output_count != sprite_count:
        raise ValueError("Output SPR signature or sprite count changed")

    expected_ids = sorted(entry["targetSpriteId"] for entry in entries)
    changed_ids = sorted(
        sprite_id
        for sprite_id in range(1, sprite_count + 1)
        if sprites.get(sprite_id) != output_sprites.get(sprite_id)
    )
    if changed_ids != expected_ids:
        raise ValueError(
            f"Changed slots differ from expected: changed={changed_ids}, expected={expected_ids}"
        )

    for entry in entries:
        target_id = int(entry["targetSpriteId"])
        decoded = decode_sprite(args.output, target_id, 32, False, False)
        original = decode_sprite(args.spr, target_id, 32, False, False)
        if decoded is None or original is None:
            raise ValueError(f"Unable to verify sprite {target_id}")
        if decoded.getchannel("A").tobytes() != original.getchannel("A").tobytes():
            raise ValueError(f"Alpha changed for target sprite {target_id}")

    sheet = Image.new("RGBA", (4 * 160, 3 * 176), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for index, entry in enumerate(entries):
        target_id = int(entry["targetSpriteId"])
        image = Image.open(sprites_dir / f"{target_id}.png").convert("RGBA")
        image = image.resize((128, 128), Image.Resampling.NEAREST)
        x = (index % 4) * 160 + 16
        y = (index // 4) * 176 + 28
        sheet.alpha_composite(image, (x, y))
        draw.text(
            ((index % 4) * 160 + 8, (index // 4) * 176 + 6),
            f"CID {entry['clientId']} / {target_id}",
            fill=(240, 240, 240, 255),
        )
    sheet_path = args.out_dir / "treated-border-contact-sheet.png"
    sheet.save(sheet_path)

    summary = {
        "dat": str(args.dat),
        "sourceSpr": str(args.spr),
        "outputSpr": str(args.output),
        "signature": signature.hex(),
        "spriteCount": sprite_count,
        "groundClientId": args.ground_client_id,
        "groundSpriteId": ground_sprite_id,
        "luminance": args.luminance,
        "saturation": args.saturation,
        "changedSpriteIds": changed_ids,
        "contactSheet": str(sheet_path),
        "entries": entries,
    }
    summary_path = args.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
