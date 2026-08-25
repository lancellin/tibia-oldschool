#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path

from teleport_destinations import (
    OTBM_ATTR_ACTION_ID,
    OTBM_ATTR_ITEM,
    OTBM_ATTR_TELE_DEST,
    OTBM_ATTR_TILE_FLAGS,
    OTBM_ATTR_UNIQUE_ID,
    OTBM_HOUSETILE,
    OTBM_ITEM,
    OTBM_TILE,
    OTBM_TILE_AREA,
    Node,
    find_mapdata,
    load_map_tree,
    save_map,
)


def rewrite_tile_ground(data: bytes, old_id: int, new_id: int, is_house: bool) -> tuple[bytes, int]:
    if len(data) < 2:
        return data, 0

    offset = 2
    if is_house:
        offset += 4

    out = bytearray(data)
    changed = 0
    while offset < len(data):
        attr = data[offset]
        offset += 1

        if attr == OTBM_ATTR_TILE_FLAGS:
            offset += 4
        elif attr in (OTBM_ATTR_ACTION_ID, OTBM_ATTR_UNIQUE_ID):
            offset += 2
        elif attr == OTBM_ATTR_TELE_DEST:
            offset += 5
        elif attr == OTBM_ATTR_ITEM:
            if offset + 2 > len(data):
                break
            (itemid,) = struct.unpack_from("<H", data, offset)
            if itemid == old_id:
                struct.pack_into("<H", out, offset, new_id)
                changed += 1
            offset += 2
        else:
            break

    return bytes(out), changed


def rewrite_item_nodes(node: Node, old_id: int, new_id: int) -> int:
    changed = 0
    if node.node_type == OTBM_ITEM and len(node.props) >= 2:
        (itemid,) = struct.unpack_from("<H", node.props, 0)
        if itemid == old_id:
            node.props = struct.pack("<H", new_id) + node.props[2:]
            changed += 1

    for child in node.children:
        changed += rewrite_item_nodes(child, old_id, new_id)
    return changed


def replace_ids(root: Node, old_id: int, new_id: int) -> int:
    mapdata = find_mapdata(root)
    changed = 0
    for area in mapdata.children:
        if area.node_type != OTBM_TILE_AREA:
            continue
        for tile in area.children:
            if tile.node_type not in (OTBM_TILE, OTBM_HOUSETILE):
                continue
            tile.props, tile_changed = rewrite_tile_ground(
                tile.props, old_id, new_id, tile.node_type == OTBM_HOUSETILE
            )
            changed += tile_changed
            for child in tile.children:
                changed += rewrite_item_nodes(child, old_id, new_id)
    return changed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Replace every occurrence of an item id placed on an OTBM map."
    )
    parser.add_argument("--map", type=Path, default=Path(r"D:\tibia-oldschool\server\data\world\world.otbm"))
    parser.add_argument("--old-id", type=int, required=True)
    parser.add_argument("--new-id", type=int, required=True)
    parser.add_argument("--backup", type=Path, help="Copy the current map to this path before writing")
    parser.add_argument("--dry-run", action="store_true", help="Count replacements without writing")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    identifier, root = load_map_tree(args.map)
    changed = replace_ids(root, args.old_id, args.new_id)
    print(f"replacements {args.old_id} -> {args.new_id}: {changed}")
    if args.dry_run:
        return 0
    if args.backup:
        args.backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.map, args.backup)
        print(f"backup: {args.backup}")
    save_map(args.map, identifier, root, backup=False)
    print(f"saved: {args.map}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
