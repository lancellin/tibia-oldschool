#!/usr/bin/env python3
import argparse
from pathlib import Path

from extract_thing_assets import THING_CATEGORY_ITEM
from patch_water_dat_spr_exact import scan_dat_records


def parse_ids(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start_text, end_text = part.split("-", 1)
            result.extend(range(int(start_text), int(end_text) + 1))
        else:
            result.append(int(part))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Copy one DAT item's geometry and sprite references to other item client IDs."
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", type=int, required=True)
    parser.add_argument("--source-client-id", type=int, required=True)
    parser.add_argument("--target-client-ids", required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    header_end, records = scan_dat_records(data, args.version)
    items = {
        record.client_id: record
        for record in records
        if record.category == THING_CATEGORY_ITEM
    }
    source = items.get(args.source_client_id)
    if source is None:
        raise ValueError(f"Source client ID {args.source_client_id} was not found")

    target_ids = parse_ids(args.target_client_ids)
    missing = sorted(set(target_ids) - set(items))
    if missing:
        raise ValueError(f"Target client IDs were not found: {missing}")

    source_geometry = data[source.geometry_start:source.end]
    wanted = set(target_ids)
    output = bytearray(data[:header_end])
    cursor = header_end

    for record in records:
        if record.start != cursor:
            raise ValueError(f"Unexpected DAT record gap before client ID {record.client_id}")
        if record.category == THING_CATEGORY_ITEM and record.client_id in wanted:
            output.extend(data[record.start:record.geometry_start])
            output.extend(source_geometry)
        else:
            output.extend(data[record.start:record.end])
        cursor = record.end

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(
        f"[ok] copied geometry from client {args.source_client_id} "
        f"to {','.join(str(client_id) for client_id in target_ids)}"
    )
    print(f"[ok] wrote {args.output} ({len(data)} -> {len(output)} bytes)")


if __name__ == "__main__":
    main()
