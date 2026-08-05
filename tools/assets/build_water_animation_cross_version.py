#!/usr/bin/env python3
import argparse
import json
import math
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from extract_map_sprites import read_otb_mapping
from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr


def evenly_spaced_frames(source_frames: int, target_frames: int) -> list[int]:
    return [
        int(math.floor(index * source_frames / target_frames + 0.5)) % source_frames
        for index in range(target_frames)
    ]


def ordered_unique(values: list[int]) -> list[int]:
    result: list[int] = []
    seen: set[int] = set()
    for value in values:
        if value <= 0 or value in seen:
            continue
        result.append(value)
        seen.add(value)
    return result


def build_phase_mosaic(
    source_images: list[Image.Image],
    source_sprite_ids: list[int],
    target_sprite_ids: list[int],
    grid_size: int,
    output: Path,
) -> list[dict[str, int]]:
    if len(source_images) != 6:
        raise ValueError("The 772 sea animation is expected to have exactly six target phases")
    if grid_size % 3 != 0 or grid_size % 2 != 0:
        raise ValueError("Grid size must be divisible by both 3 and 2")

    tile_size = 32
    block_width = grid_size // 3
    block_height = grid_size // 2
    mosaic = Image.new("RGBA", (grid_size * tile_size, grid_size * tile_size), (0, 0, 0, 0))
    placements: list[dict[str, int]] = []

    for phase, image in enumerate(source_images):
        block_x = phase % 3
        block_y = phase // 3
        for local_y in range(block_height):
            for local_x in range(block_width):
                tile_x = block_x * block_width + local_x
                tile_y = block_y * block_height + local_y
                mosaic.alpha_composite(image, (tile_x * tile_size, tile_y * tile_size))
                placements.append(
                    {
                        "phase": phase,
                        "tileX": tile_x,
                        "tileY": tile_y,
                        "sourceSpriteId": source_sprite_ids[phase],
                        "targetSpriteId": target_sprite_ids[phase],
                    }
                )

    output.parent.mkdir(parents=True, exist_ok=True)
    mosaic.save(output)
    return placements


def build_contact_sheet(
    images: list[Image.Image],
    source_frame_ids: list[int],
    source_sprite_ids: list[int],
    target_sprite_ids: list[int],
    output: Path,
) -> None:
    scale = 4
    tile_size = 32 * scale
    label_height = 30
    margin = 10
    font = ImageFont.load_default()
    sheet = Image.new(
        "RGBA",
        (margin * 2 + len(images) * tile_size, margin * 2 + tile_size + label_height),
        (30, 30, 30, 255),
    )
    draw = ImageDraw.Draw(sheet)

    for phase, image in enumerate(images):
        x = margin + phase * tile_size
        y = margin
        scaled = image.resize((tile_size, tile_size), Image.Resampling.NEAREST)
        sheet.alpha_composite(scaled, (x, y))
        draw.rectangle((x, y, x + tile_size - 1, y + tile_size - 1), outline=(110, 110, 110, 255))
        draw.text(
            (x + 2, y + tile_size + 2),
            f"P{phase} F{source_frame_ids[phase]} S{source_sprite_ids[phase]} -> {target_sprite_ids[phase]}",
            fill=(240, 240, 240, 255),
            font=font,
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(output, quality=95)


def patch_classic_spr(
    source_spr: Path,
    target_spr: Path,
    output: Path,
    source_sprite_ids: list[int],
    target_sprite_ids: list[int],
) -> None:
    _, _, source_sprites = read_spr(source_spr)
    target_signature, target_count, target_sprites = read_spr(target_spr)
    patched = dict(target_sprites)

    for source_id, target_id in zip(source_sprite_ids, target_sprite_ids, strict=True):
        if source_id not in source_sprites:
            raise ValueError(f"Source sprite {source_id} is missing from {source_spr}")
        if target_id <= 0 or target_id > target_count:
            raise ValueError(f"Target sprite {target_id} is outside 1-{target_count}")
        patched[target_id] = source_sprites[source_id]

    write_spr(output, target_signature, target_count, patched)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare and optionally patch the animated 7.4 sea into the six shared 7.72 sea phases."
    )
    parser.add_argument("--source-dat", type=Path, required=True)
    parser.add_argument("--source-spr", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, required=True)
    parser.add_argument("--target-otb", type=Path, required=True)
    parser.add_argument("--source-client-id", type=int, default=490)
    parser.add_argument("--target-server-ids", default="4608-4625")
    parser.add_argument("--source-pattern", type=int, default=0)
    parser.add_argument("--grid", type=int, default=24)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--patch-target-spr", type=Path)
    parser.add_argument("--patched-spr-output", type=Path)
    args = parser.parse_args()

    if bool(args.patch_target_spr) != bool(args.patched_spr_output):
        raise ValueError("--patch-target-spr and --patched-spr-output must be used together")

    start_text, end_text = args.target_server_ids.split("-", 1)
    target_server_ids = list(range(int(start_text), int(end_text) + 1))
    server_to_client = read_otb_mapping(args.target_otb)
    target_client_ids = [server_to_client[server_id] for server_id in target_server_ids]

    source = parse_dat(args.source_dat, 740, THING_CATEGORY_ITEM, {args.source_client_id})[
        args.source_client_id
    ]
    targets = parse_dat(args.target_dat, 772, THING_CATEGORY_ITEM, set(target_client_ids))

    source_pattern_count = source.pattern_x * source.pattern_y * source.pattern_z
    if source.width != 1 or source.height != 1 or source.layers != 1:
        raise ValueError("Expected the source sea item to be a one-layer 1x1 ground")
    if not 0 <= args.source_pattern < source_pattern_count:
        raise ValueError(f"Source pattern must be in 0-{source_pattern_count - 1}")

    target_sprite_ids = ordered_unique(
        [sprite_id for client_id in target_client_ids for sprite_id in targets[client_id].sprites]
    )
    if len(target_sprite_ids) != 6:
        raise ValueError(f"Expected six shared target sea sprites, found {target_sprite_ids}")
    if any(targets[client_id].frames != 6 for client_id in target_client_ids):
        raise ValueError("Every target sea client item must contain six animation phases")

    source_frame_ids = evenly_spaced_frames(source.frames, len(target_sprite_ids))
    source_sprite_ids = [
        source.sprites[frame_id * source_pattern_count + args.source_pattern]
        for frame_id in source_frame_ids
    ]

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    source_tiles = args.out_root / "source-tiles"
    source_tiles.mkdir(parents=True, exist_ok=True)

    source_images: list[Image.Image] = []
    for phase, source_sprite_id in enumerate(source_sprite_ids):
        image = decode_sprite(args.source_spr, source_sprite_id, 32, False, False)
        if image is None:
            raise ValueError(f"Could not decode source sprite {source_sprite_id}")
        image = image.convert("RGBA")
        image.save(source_tiles / f"phase-{phase}-source-{source_sprite_id}-target-{target_sprite_ids[phase]}.png")
        source_images.append(image)

    mosaic_path = args.out_root / "water-740-animation-to-772-mosaic-24x24-1x.png"
    placements = build_phase_mosaic(
        source_images,
        source_sprite_ids,
        target_sprite_ids,
        args.grid,
        mosaic_path,
    )
    contact_sheet_path = args.out_root / "water-740-animation-to-772-contact-sheet.png"
    build_contact_sheet(
        source_images,
        source_frame_ids,
        source_sprite_ids,
        target_sprite_ids,
        contact_sheet_path,
    )

    manifest = {
        "type": "animated-ground",
        "sourceDatVersion": 740,
        "targetDatVersion": 772,
        "sourceClientId": args.source_client_id,
        "sourcePattern": args.source_pattern,
        "sourcePatternCount": source_pattern_count,
        "sourceAnimationFrames": source.frames,
        "selectedSourceFrameIds": source_frame_ids,
        "selectedSourceSpriteIds": source_sprite_ids,
        "targetServerIds": target_server_ids,
        "targetClientIds": target_client_ids,
        "targetSpriteIds": target_sprite_ids,
        "tileSize": 32,
        "grid": {"width": args.grid, "height": args.grid},
        "phaseBlocks": {"columns": 3, "rows": 2},
        "mosaic": str(mosaic_path),
        "contactSheet": str(contact_sheet_path),
        "placements": placements,
    }

    if args.patch_target_spr and args.patched_spr_output:
        patch_classic_spr(
            args.source_spr,
            args.patch_target_spr,
            args.patched_spr_output,
            source_sprite_ids,
            target_sprite_ids,
        )
        manifest["patchedSpr"] = str(args.patched_spr_output)

    manifest_path = args.out_root / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        json.dumps(
            {
                "mosaic": str(mosaic_path),
                "contactSheet": str(contact_sheet_path),
                "manifest": str(manifest_path),
                "sourceFrameIds": source_frame_ids,
                "sourceSpriteIds": source_sprite_ids,
                "targetSpriteIds": target_sprite_ids,
                "patchedSpr": str(args.patched_spr_output) if args.patched_spr_output else None,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
