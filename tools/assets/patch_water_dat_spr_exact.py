#!/usr/bin/env python3
import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import (
    THING_CATEGORY_ITEM,
    Reader,
    adjust_attr_for_version,
    parse_dat,
    skip_attr_payload,
)
from merge_cwm import read_cwm, write_cwm
from patch_classic_spr_cross_version import read_spr, write_spr


@dataclass
class DatRecord:
    category: int
    client_id: int
    start: int
    geometry_start: int
    end: int


def scan_dat_records(data: bytes, version: int) -> tuple[int, list[DatRecord]]:
    reader = Reader(data)
    reader.get_u32()
    category_counts = [reader.get_u16() + 1 for _ in range(4)]
    header_end = reader.offset
    records: list[DatRecord] = []

    for category, count in enumerate(category_counts):
        first_id = 100 if category == THING_CATEGORY_ITEM else 1
        for client_id in range(first_id, count):
            start = reader.offset
            for _ in range(255):
                raw_attr = reader.get_u8()
                if raw_attr == 255:
                    break
                attr = adjust_attr_for_version(raw_attr, version)
                skip_attr_payload(reader, attr, version)
            else:
                raise ValueError(f"Thing {client_id} did not terminate attributes")

            geometry_start = reader.offset
            width = reader.get_u8()
            height = reader.get_u8()
            if width > 1 or height > 1:
                reader.get_u8()
            layers = reader.get_u8()
            pattern_x = reader.get_u8()
            pattern_y = reader.get_u8()
            pattern_z = reader.get_u8() if version >= 755 else 1
            frames = reader.get_u8()
            sprite_bytes = 4 if version >= 960 else 2
            sprite_count = width * height * layers * pattern_x * pattern_y * pattern_z * frames
            reader.offset += sprite_count * sprite_bytes
            if reader.offset > len(data):
                raise ValueError(f"Thing {client_id} extends past the end of the DAT")
            records.append(DatRecord(category, client_id, start, geometry_start, reader.offset))

    if reader.offset != len(data):
        raise ValueError(f"DAT scan ended at {reader.offset}, file has {len(data)} bytes")
    return header_end, records


def patch_dat(
    source_dat: Path,
    target_dat: Path,
    output_dat: Path,
    source_client_id: int,
    target_client_ids: list[int],
    source_to_target_sprite: dict[int, int],
) -> dict[str, object]:
    source_thing = parse_dat(source_dat, 740, THING_CATEGORY_ITEM, {source_client_id})[source_client_id]
    if (
        source_thing.width,
        source_thing.height,
        source_thing.layers,
        source_thing.pattern_x,
        source_thing.pattern_y,
        source_thing.pattern_z,
        source_thing.frames,
    ) != (1, 1, 1, 4, 2, 1, 16):
        raise ValueError("Source water item is not the expected 1x1, 4x2-pattern, 16-frame layout")

    remapped_sprites = [source_to_target_sprite[sprite_id] for sprite_id in source_thing.sprites]
    geometry = bytearray((1, 1, 1, 4, 2, 1, 16))
    for sprite_id in remapped_sprites:
        geometry.extend(struct.pack("<H", sprite_id))

    target_data = target_dat.read_bytes()
    header_end, records = scan_dat_records(target_data, 772)
    wanted = set(target_client_ids)
    found: set[int] = set()
    output = bytearray(target_data[:header_end])
    cursor = header_end

    for record in records:
        if record.start != cursor:
            raise ValueError(f"Unexpected DAT record gap before client id {record.client_id}")
        if record.category == THING_CATEGORY_ITEM and record.client_id in wanted:
            output.extend(target_data[record.start:record.geometry_start])
            output.extend(geometry)
            found.add(record.client_id)
        else:
            output.extend(target_data[record.start:record.end])
        cursor = record.end

    missing = sorted(wanted - found)
    if missing:
        raise ValueError(f"Target client ids were not found in DAT: {missing}")

    output_dat.parent.mkdir(parents=True, exist_ok=True)
    output_dat.write_bytes(output)
    return {
        "inputBytes": len(target_data),
        "outputBytes": len(output),
        "patchedClientIds": sorted(found),
        "sourceSpriteReferences": len(source_thing.sprites),
        "targetSpriteReferences": len(remapped_sprites),
    }


def append_source_sprites(
    source_spr: Path,
    target_spr: Path,
    output_spr: Path,
    source_sprite_ids: list[int],
) -> tuple[dict[int, int], dict[str, object]]:
    source_signature, source_count, source_sprites = read_spr(source_spr)
    target_signature, target_count, target_sprites = read_spr(target_spr)
    source_to_target: dict[int, int] = {}
    patched = dict(target_sprites)

    for offset, source_id in enumerate(source_sprite_ids, start=1):
        if source_id not in source_sprites:
            raise ValueError(f"Source sprite {source_id} is missing from {source_spr}")
        target_id = target_count + offset
        source_to_target[source_id] = target_id
        patched[target_id] = source_sprites[source_id]

    output_count = target_count + len(source_sprite_ids)
    write_spr(output_spr, target_signature, output_count, patched)
    return source_to_target, {
        "sourceSignature": source_signature.hex(),
        "sourceSpriteCount": source_count,
        "targetSignature": target_signature.hex(),
        "inputSpriteCount": target_count,
        "outputSpriteCount": output_count,
        "appendedCount": len(source_sprite_ids),
    }


def update_cwm_count(input_cwm: Path, output_cwm: Path, sprite_count: int) -> dict[str, object]:
    version, old_count, sprites = read_cwm(input_cwm)
    write_cwm(output_cwm, version, sprite_count, sprites)
    return {
        "version": version,
        "inputSpriteCount": old_count,
        "outputSpriteCount": sprite_count,
        "entryCount": len(sprites),
        "entriesPreserved": sorted(sprites),
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch 772 sea items to use the exact 740 4x2-pattern, 16-frame water layout."
    )
    parser.add_argument("--source-dat", type=Path, required=True)
    parser.add_argument("--source-spr", type=Path, required=True)
    parser.add_argument("--target-dat", type=Path, required=True)
    parser.add_argument("--target-spr", type=Path, required=True)
    parser.add_argument("--target-cwm", type=Path, required=True)
    parser.add_argument("--target-otb", type=Path, required=True)
    parser.add_argument("--source-client-id", type=int, default=490)
    parser.add_argument("--target-server-ids", default="4608-4625")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    start_text, end_text = args.target_server_ids.split("-", 1)
    target_server_ids = list(range(int(start_text), int(end_text) + 1))
    server_to_client = read_otb_mapping(args.target_otb)
    target_client_ids = [server_to_client[server_id] for server_id in target_server_ids]
    if len(set(target_client_ids)) != len(target_client_ids):
        raise ValueError("Target server ids do not map to unique client ids")

    source_thing = parse_dat(
        args.source_dat,
        740,
        THING_CATEGORY_ITEM,
        {args.source_client_id},
    )[args.source_client_id]
    source_sprite_ids: list[int] = []
    seen: set[int] = set()
    for sprite_id in source_thing.sprites:
        if sprite_id > 0 and sprite_id not in seen:
            source_sprite_ids.append(sprite_id)
            seen.add(sprite_id)
    if len(source_sprite_ids) != 32:
        raise ValueError(f"Expected 32 unique source water sprites, found {len(source_sprite_ids)}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_spr = args.output_dir / "Tibia.water-740-exact.spr"
    output_dat = args.output_dir / "Tibia.water-740-exact.dat"
    output_cwm = args.output_dir / "Tibia.water-740-exact.cwm"

    source_to_target, spr_summary = append_source_sprites(
        args.source_spr,
        args.target_spr,
        output_spr,
        source_sprite_ids,
    )
    dat_summary = patch_dat(
        args.source_dat,
        args.target_dat,
        output_dat,
        args.source_client_id,
        target_client_ids,
        source_to_target,
    )
    cwm_summary = update_cwm_count(
        args.target_cwm,
        output_cwm,
        spr_summary["outputSpriteCount"],
    )

    summary = {
        "sourceClientId": args.source_client_id,
        "targetServerIds": target_server_ids,
        "targetClientIds": target_client_ids,
        "sourceSpriteIds": source_sprite_ids,
        "sourceToAppendedTargetSprite": {
            str(source_id): target_id for source_id, target_id in source_to_target.items()
        },
        "outputs": {
            "dat": str(output_dat),
            "spr": str(output_spr),
            "cwm": str(output_cwm),
        },
        "dat": dat_summary,
        "spr": spr_summary,
        "cwm": cwm_summary,
    }
    summary_path = args.output_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(
        json.dumps(
            {
                "dat": str(output_dat),
                "spr": str(output_spr),
                "cwm": str(output_cwm),
                "summary": str(summary_path),
                "targetClientIds": target_client_ids,
                "appendedSprites": len(source_sprite_ids),
                "outputSpriteCount": spr_summary["outputSpriteCount"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
