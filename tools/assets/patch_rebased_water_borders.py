#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path

from PIL import Image

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from merge_cwm import read_cwm, write_cwm
from patch_classic_spr_cross_version import read_spr, write_spr
from patch_water_dat_spr_exact import scan_dat_records
from rebase_water_borders import SprReader, remove_old_water, replace_only_old_water, sprite_index


def encode_sprite(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    pixels = list(rgba.getdata())
    payload = bytearray()
    index = 0

    while index < len(pixels):
        transparent = 0
        while index < len(pixels) and pixels[index][3] == 0 and transparent < 0xFFFF:
            transparent += 1
            index += 1

        colored_start = index
        colored = 0
        while index < len(pixels) and pixels[index][3] != 0 and colored < 0xFFFF:
            colored += 1
            index += 1

        if transparent == 0 and colored == 0:
            break
        payload.extend(struct.pack("<HH", transparent, colored))
        for pixel in pixels[colored_start:colored_start + colored]:
            payload.extend(pixel[:3])

    if len(payload) > 0xFFFF:
        raise ValueError("Encoded sprite payload exceeds U16 size")
    return b"\x00\xff\x00" + struct.pack("<H", len(payload)) + payload


def build_geometry(pattern_x: int, pattern_y: int, sprite_ids: list[int]) -> bytes:
    expected = pattern_x * pattern_y * 16
    if len(sprite_ids) != expected:
        raise ValueError(f"Expected {expected} sprite references, found {len(sprite_ids)}")
    geometry = bytearray((1, 1, 1, pattern_x, pattern_y, 1, 16))
    for sprite_id in sprite_ids:
        geometry.extend(struct.pack("<H", sprite_id))
    return bytes(geometry)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Append rebased water-border sprites and patch their DAT items to synchronized water layouts."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--cwm", type=Path, required=True)
    parser.add_argument("--otb", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--server-start", type=int, default=4632)
    parser.add_argument("--server-end", type=int, default=4663)
    parser.add_argument("--water-client-id", type=int, default=4597)
    parser.add_argument("--version", type=int, default=772)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    server_to_client = read_otb_mapping(args.otb)
    mappings = [
        (server_id, server_to_client[server_id])
        for server_id in range(args.server_start, args.server_end + 1)
    ]
    target_client_ids = {client_id for _, client_id in mappings}
    things = parse_dat(
        args.dat,
        args.version,
        THING_CATEGORY_ITEM,
        target_client_ids | {args.water_client_id},
    )
    water = things[args.water_client_id]
    if (water.pattern_x, water.pattern_y, water.frames) != (4, 2, 16):
        raise ValueError("Active water is not the expected 4x2-pattern, 16-frame layout")

    spr_reader = SprReader(args.spr)
    water_images = [spr_reader.decode(sprite_id) for sprite_id in water.sprites]
    signature, old_sprite_count, old_sprites = read_spr(args.spr)
    new_sprites = dict(old_sprites)
    next_sprite_id = old_sprite_count + 1
    geometries: dict[int, bytes] = {}
    item_summaries: list[dict[str, object]] = []

    for server_id, client_id in mappings:
        thing = things[client_id]
        source_images = [spr_reader.decode(sprite_id) for sprite_id in thing.sprites]
        overlays_masks = [remove_old_water(image) for image in source_images]
        combined_x = math.lcm(4, thing.pattern_x)
        combined_y = math.lcm(2, thing.pattern_y)
        item_sprite_ids: list[int] = []

        for phase in range(16):
            source_phase = min(thing.frames - 1, phase * thing.frames // 16)
            for py in range(combined_y):
                source_py = py % thing.pattern_y
                water_py = py % 2
                for px in range(combined_x):
                    source_px = px % thing.pattern_x
                    water_px = px % 4
                    source_index = (
                        source_phase * thing.pattern_x * thing.pattern_y
                        + source_py * thing.pattern_x
                        + source_px
                    )
                    water_index = sprite_index(4, 2, phase, water_px, water_py)
                    overlay, water_mask = overlays_masks[source_index]
                    composed = replace_only_old_water(
                        source_images[source_index],
                        overlay,
                        water_mask,
                        water_images[water_index],
                    )
                    new_sprites[next_sprite_id] = encode_sprite(composed)
                    item_sprite_ids.append(next_sprite_id)
                    next_sprite_id += 1

        geometries[client_id] = build_geometry(combined_x, combined_y, item_sprite_ids)
        item_summaries.append(
            {
                "serverId": server_id,
                "clientId": client_id,
                "sourceLayout": [thing.pattern_x, thing.pattern_y, thing.frames],
                "targetLayout": [combined_x, combined_y, 16],
                "spriteStart": item_sprite_ids[0],
                "spriteEnd": item_sprite_ids[-1],
                "spriteCount": len(item_sprite_ids),
            }
        )

    output_sprite_count = next_sprite_id - 1
    if output_sprite_count > 0xFFFF:
        raise ValueError("Output sprite count exceeds the U16 SPR limit")

    output_spr = args.output_dir / "Tibia.water-borders-rebased.spr"
    write_spr(output_spr, signature, output_sprite_count, new_sprites)

    dat_data = args.dat.read_bytes()
    header_end, records = scan_dat_records(dat_data, args.version)
    output_dat_data = bytearray(dat_data[:header_end])
    changed: list[int] = []
    cursor = header_end
    for record in records:
        if record.start != cursor:
            raise ValueError(f"Unexpected DAT gap before client {record.client_id}")
        geometry = geometries.get(record.client_id) if record.category == THING_CATEGORY_ITEM else None
        if geometry is None:
            output_dat_data.extend(dat_data[record.start:record.end])
        else:
            output_dat_data.extend(dat_data[record.start:record.geometry_start])
            output_dat_data.extend(geometry)
            changed.append(record.client_id)
        cursor = record.end

    output_dat = args.output_dir / "Tibia.water-borders-rebased.dat"
    output_dat.write_bytes(output_dat_data)

    cwm_version, old_cwm_count, cwm_entries = read_cwm(args.cwm)
    output_cwm = args.output_dir / "Tibia.water-borders-rebased.cwm"
    write_cwm(output_cwm, cwm_version, output_sprite_count, cwm_entries)

    summary = {
        "targetServerIds": [server_id for server_id, _ in mappings],
        "targetClientIds": [client_id for _, client_id in mappings],
        "changedClientIds": changed,
        "inputSpriteCount": old_sprite_count,
        "outputSpriteCount": output_sprite_count,
        "appendedSpriteCount": output_sprite_count - old_sprite_count,
        "inputCwmCount": old_cwm_count,
        "outputCwmCount": output_sprite_count,
        "cwmEntriesPreserved": len(cwm_entries),
        "items": item_summaries,
        "outputs": {
            "dat": str(output_dat),
            "spr": str(output_spr),
            "cwm": str(output_cwm),
        },
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
