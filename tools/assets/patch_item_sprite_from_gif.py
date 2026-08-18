#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr
from patch_rebased_water_borders import encode_sprite


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_gif_rgba(path: Path, sprite_size: int) -> Image.Image:
    image = Image.open(path)
    frames = getattr(image, "n_frames", 1)
    if frames != 1:
        raise ValueError(f"{path} has {frames} frames; only single-frame sprites are supported")
    image = image.convert("RGBA")
    if image.size != (sprite_size, sprite_size):
        raise ValueError(
            f"{path} is {image.size[0]}x{image.size[1]}; expected {sprite_size}x{sprite_size}"
        )
    return image


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Replace a single-sprite 772 item slot with a GIF, preserving transparency."
    )
    parser.add_argument("--gif", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, required=True)
    parser.add_argument("--target-spr", type=Path, required=True)
    parser.add_argument("--client-id", type=int, required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--sprite-size", type=int, default=32)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    things = parse_dat(
        args.target_dat,
        version=args.version,
        category=THING_CATEGORY_ITEM,
        target_ids={args.client_id},
    )
    thing = things.get(args.client_id)
    if thing is None:
        raise ValueError(f"Client ID {args.client_id} not found in {args.target_dat}")
    if (thing.pattern_x, thing.pattern_y, thing.frames) != (1, 1, 1):
        raise ValueError(
            f"Client ID {args.client_id} layout is "
            f"{thing.pattern_x}x{thing.pattern_y}x{thing.frames}; only 1x1x1 items are supported"
        )
    if len(thing.unique_sprites) != 1:
        raise ValueError(
            f"Client ID {args.client_id} references {len(thing.unique_sprites)} sprite slots"
        )
    target_id = thing.unique_sprites[0]

    signature, sprite_count, sprites = read_spr(args.target_spr)
    if target_id > sprite_count:
        raise ValueError(f"Target sprite {target_id} is outside 1-{sprite_count}")
    previous_block = sprites.get(target_id)
    if previous_block is None:
        raise ValueError(f"Sprite slot {target_id} is empty in {args.target_spr}")

    image = load_gif_rgba(args.gif, args.sprite_size)
    new_block = encode_sprite(image)

    patched = dict(sprites)
    patched[target_id] = new_block
    write_spr(args.output, signature, sprite_count, patched)

    output_signature, output_count, output_sprites = read_spr(args.output)
    if output_signature != signature or output_count != sprite_count:
        raise ValueError("Output SPR signature or sprite count changed")
    changed_ids = sorted(
        sprite_id
        for sprite_id in range(1, sprite_count + 1)
        if sprites.get(sprite_id) != output_sprites.get(sprite_id)
    )
    if changed_ids != [target_id]:
        raise ValueError(f"Changed slots differ from expected: changed={changed_ids}")
    if output_sprites[target_id] != new_block:
        raise ValueError(f"Sprite slot {target_id} does not match the encoded GIF block")

    summary = {
        "gif": str(args.gif),
        "gifSha256": sha256(args.gif.read_bytes()),
        "clientId": args.client_id,
        "targetDat": str(args.target_dat),
        "targetSpr": str(args.target_spr),
        "targetSignature": signature.hex(),
        "targetSpriteCount": sprite_count,
        "targetSpriteId": target_id,
        "previousBlockSha256": sha256(previous_block),
        "previousBlockSize": len(previous_block),
        "newBlockSha256": sha256(new_block),
        "newBlockSize": len(new_block),
        "output": str(args.output),
        "changedSpriteIds": changed_ids,
    }
    summary_path = args.output.with_suffix(".summary.json")
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
