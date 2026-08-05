#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import shutil
import struct
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


OTB_NODE_ESCAPE = 0xFD
OTB_NODE_START = 0xFE
OTB_NODE_END = 0xFF

OTBM_MAP_DATA = 2
OTBM_TILE_AREA = 4
OTBM_TILE = 5
OTBM_ITEM = 6
OTBM_HOUSETILE = 14

OTBM_ATTR_TILE_FLAGS = 3
OTBM_ATTR_ACTION_ID = 4
OTBM_ATTR_UNIQUE_ID = 5
OTBM_ATTR_TELE_DEST = 8
OTBM_ATTR_ITEM = 9


@dataclass
class Node:
    node_type: int
    props: bytes
    children: list["Node"] = field(default_factory=list)


@dataclass
class TeleportEntry:
    source_x: int
    source_y: int
    source_z: int
    itemid: int | None
    actionid: int
    uniqueid: int
    dest_x: int | None
    dest_y: int | None
    dest_z: int | None
    origin: str
    teleport_kind: str
    movement_script: str
    item_node: Node | None = None

    def to_row(self) -> dict[str, object]:
        return {
            "source_x": self.source_x,
            "source_y": self.source_y,
            "source_z": self.source_z,
            "itemid": self.itemid,
            "actionid": self.actionid,
            "uniqueid": self.uniqueid,
            "dest_x": self.dest_x,
            "dest_y": self.dest_y,
            "dest_z": self.dest_z,
            "origin": self.origin,
            "teleport_kind": self.teleport_kind,
            "movement_script": self.movement_script,
        }


def unescape_props(raw: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(raw):
        value = raw[i]
        if value == OTB_NODE_ESCAPE and i + 1 < len(raw):
            out.append(raw[i + 1])
            i += 2
            continue
        out.append(value)
        i += 1
    return bytes(out)


def escape_props(raw: bytes) -> bytes:
    out = bytearray()
    for value in raw:
        if value in (OTB_NODE_ESCAPE, OTB_NODE_START, OTB_NODE_END):
            out.append(OTB_NODE_ESCAPE)
        out.append(value)
    return bytes(out)


def parse_node(data: bytes, offset: int) -> tuple[Node, int]:
    if offset >= len(data) or data[offset] != OTB_NODE_START:
        raise ValueError(f"expected node start at offset {offset}")
    if offset + 1 >= len(data):
        raise ValueError("truncated node type")

    node_type = data[offset + 1]
    pos = offset + 2
    props = bytearray()
    children: list[Node] = []

    while pos < len(data):
        value = data[pos]
        if value == OTB_NODE_ESCAPE:
            if pos + 1 >= len(data):
                raise ValueError("dangling escape byte")
            props.append(value)
            props.append(data[pos + 1])
            pos += 2
            continue
        if value == OTB_NODE_START:
            child, pos = parse_node(data, pos)
            children.append(child)
            continue
        if value == OTB_NODE_END:
            return Node(node_type=node_type, props=unescape_props(bytes(props)), children=children), pos + 1
        props.append(value)
        pos += 1

    raise ValueError("node not terminated")


def serialize_node(node: Node) -> bytes:
    out = bytearray()
    out.append(OTB_NODE_START)
    out.append(node.node_type)
    out.extend(escape_props(node.props))
    for child in node.children:
        out.extend(serialize_node(child))
    out.append(OTB_NODE_END)
    return bytes(out)


def read_u8(data: bytes, offset: int) -> tuple[int, int]:
    return data[offset], offset + 1


def read_u16(data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<H", data, offset)[0], offset + 2


def read_u32(data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<I", data, offset)[0], offset + 4


def skip_string(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        return len(data)
    length = struct.unpack_from("<H", data, offset)[0]
    offset += 2
    return min(len(data), offset + length)


def parse_item_payload(data: bytes) -> dict[str, object]:
    result: dict[str, object] = {
        "itemid": None,
        "actionid": 0,
        "uniqueid": 0,
        "tele_dest": None,
    }
    if len(data) < 2:
        return result

    offset = 0
    itemid, offset = read_u16(data, offset)
    result["itemid"] = itemid

    while offset < len(data):
        attr = data[offset]
        offset += 1

        if attr == OTBM_ATTR_ACTION_ID:
            if offset + 2 > len(data):
                break
            result["actionid"], offset = read_u16(data, offset)
        elif attr == OTBM_ATTR_UNIQUE_ID:
            if offset + 2 > len(data):
                break
            result["uniqueid"], offset = read_u16(data, offset)
        elif attr == OTBM_ATTR_TELE_DEST:
            if offset + 5 > len(data):
                break
            x, offset = read_u16(data, offset)
            y, offset = read_u16(data, offset)
            z, offset = read_u8(data, offset)
            result["tele_dest"] = {"x": x, "y": y, "z": z}
        elif attr in (6, 7):
            offset = skip_string(data, offset)
        elif attr in (10, 22):
            if offset + 2 > len(data):
                break
            _, offset = read_u16(data, offset)
        elif attr in (14, 15, 17, 12):
            if offset + 1 > len(data):
                break
            _, offset = read_u8(data, offset)
        elif attr in (16, 18, 19, 20, 21, 23):
            if offset + 4 > len(data):
                break
            _, offset = read_u32(data, offset)
        elif attr == 128:
            break
        else:
            break

    return result


def replace_item_teledest(data: bytes, dest: tuple[int, int, int]) -> bytes:
    if len(data) < 2:
        raise ValueError("item payload too short")

    offset = 2
    while offset < len(data):
        attr = data[offset]
        offset += 1

        if attr == OTBM_ATTR_TELE_DEST:
            if offset + 5 > len(data):
                raise ValueError("truncated teleport destination")
            payload = struct.pack("<HHB", dest[0], dest[1], dest[2])
            return data[:offset] + payload + data[offset + 5 :]
        if attr in (OTBM_ATTR_ACTION_ID, OTBM_ATTR_UNIQUE_ID, 10, 22):
            offset += 2
        elif attr in (14, 15, 17, 12):
            offset += 1
        elif attr in (16, 18, 19, 20, 21, 23):
            offset += 4
        elif attr in (6, 7):
            offset = skip_string(data, offset)
        elif attr == 128:
            break
        else:
            break

    payload = struct.pack("<BHHB", OTBM_ATTR_TELE_DEST, dest[0], dest[1], dest[2])
    return data + payload


def parse_tile_payload(data: bytes, is_house: bool) -> dict[str, object]:
    result: dict[str, object] = {
        "local_x": 0,
        "local_y": 0,
        "actionid": 0,
        "uniqueid": 0,
        "tele_dest": None,
        "ground_item": None,
    }
    if len(data) < 2:
        return result

    offset = 0
    result["local_x"], offset = read_u8(data, offset)
    result["local_y"], offset = read_u8(data, offset)
    if is_house and offset + 4 <= len(data):
        _, offset = read_u32(data, offset)

    while offset < len(data):
        attr = data[offset]
        offset += 1

        if attr == OTBM_ATTR_TILE_FLAGS:
            if offset + 4 > len(data):
                break
            _, offset = read_u32(data, offset)
        elif attr == OTBM_ATTR_ACTION_ID:
            if offset + 2 > len(data):
                break
            result["actionid"], offset = read_u16(data, offset)
        elif attr == OTBM_ATTR_UNIQUE_ID:
            if offset + 2 > len(data):
                break
            result["uniqueid"], offset = read_u16(data, offset)
        elif attr == OTBM_ATTR_TELE_DEST:
            if offset + 5 > len(data):
                break
            x, offset = read_u16(data, offset)
            y, offset = read_u16(data, offset)
            z, offset = read_u8(data, offset)
            result["tele_dest"] = {"x": x, "y": y, "z": z}
        elif attr == OTBM_ATTR_ITEM:
            if offset + 2 > len(data):
                break
            itemid, offset = read_u16(data, offset)
            result["ground_item"] = {"itemid": itemid}
        else:
            break

    return result


def iter_id_spec(spec: str) -> list[int]:
    values: list[int] = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            start_s, end_s = chunk.split("-", 1)
            start = int(start_s)
            end = int(end_s)
            step = 1 if end >= start else -1
            values.extend(range(start, end + step, step))
        else:
            values.append(int(chunk))
    return values


def load_teleport_itemids(items_xml: Path) -> dict[int, str]:
    root = ET.parse(items_xml).getroot()
    result: dict[int, str] = {}
    for item in root.findall(".//item"):
        spec = item.get("id")
        if not spec:
            continue
        attrs = {attr.get("key"): attr.get("value") for attr in item.findall("attribute")}
        if attrs.get("type") != "teleport":
            continue
        name = item.get("name", "")
        for itemid in iter_id_spec(spec):
            result[itemid] = name
    return result


def load_movement_teleport_aids(movements_xml: Path) -> dict[int, str]:
    root = ET.parse(movements_xml).getroot()
    result: dict[int, str] = {}
    for move in root.findall(".//movevent"):
        aid = move.get("actionid")
        script = move.get("script")
        event = move.get("event")
        if not aid or not script or event != "StepIn":
            continue
        if "teleport" not in script.lower() and "poi_" not in script.lower():
            continue
        result[int(aid)] = script
    return result


def load_map_tree(otbm_path: Path) -> tuple[bytes, Node]:
    data = otbm_path.read_bytes()
    if len(data) < 6:
        raise ValueError(f"{otbm_path} too small")
    identifier = data[:4]
    if identifier not in (b"OTBM", b"\x00\x00\x00\x00"):
        raise ValueError(f"unexpected OTBM identifier {identifier!r}")
    root, _ = parse_node(data, 4)
    return identifier, root


def find_mapdata(root: Node) -> Node:
    for child in root.children:
        if child.node_type == OTBM_MAP_DATA:
            return child
    raise ValueError("map data node not found")


def collect_entries(root: Node, teleport_itemids: dict[int, str], movement_aids: dict[int, str]) -> list[TeleportEntry]:
    mapdata = find_mapdata(root)
    entries: list[TeleportEntry] = []

    for area in mapdata.children:
        if area.node_type != OTBM_TILE_AREA or len(area.props) < 5:
            continue
        base_x = struct.unpack_from("<H", area.props, 0)[0]
        base_y = struct.unpack_from("<H", area.props, 2)[0]
        base_z = area.props[4]

        for tile in area.children:
            if tile.node_type not in (OTBM_TILE, OTBM_HOUSETILE):
                continue

            tile_info = parse_tile_payload(tile.props, tile.node_type == OTBM_HOUSETILE)
            x = base_x + int(tile_info["local_x"])
            y = base_y + int(tile_info["local_y"])
            z = base_z

            for item_node in tile.children:
                if item_node.node_type != OTBM_ITEM:
                    continue
                item_info = parse_item_payload(item_node.props)
                itemid = item_info.get("itemid")
                if itemid is None:
                    continue

                actionid = int(item_info.get("actionid") or 0)
                uniqueid = int(item_info.get("uniqueid") or 0)
                tele_dest = item_info.get("tele_dest")
                if tele_dest:
                    dest_x = tele_dest["x"]
                    dest_y = tele_dest["y"]
                    dest_z = tele_dest["z"]
                else:
                    dest_x = dest_y = dest_z = None

                if itemid in teleport_itemids:
                    entries.append(
                        TeleportEntry(
                            source_x=x,
                            source_y=y,
                            source_z=z,
                            itemid=int(itemid),
                            actionid=actionid,
                            uniqueid=uniqueid,
                            dest_x=dest_x,
                            dest_y=dest_y,
                            dest_z=dest_z,
                            origin="item_node",
                            teleport_kind="item_type_teleport",
                            movement_script="",
                            item_node=item_node,
                        )
                    )

                if actionid in movement_aids:
                    entries.append(
                        TeleportEntry(
                            source_x=x,
                            source_y=y,
                            source_z=z,
                            itemid=int(itemid),
                            actionid=actionid,
                            uniqueid=uniqueid,
                            dest_x=dest_x,
                            dest_y=dest_y,
                            dest_z=dest_z,
                            origin="item_node",
                            teleport_kind="movement_action_teleport",
                            movement_script=movement_aids[actionid],
                            item_node=item_node,
                        )
                    )

            tile_actionid = int(tile_info.get("actionid") or 0)
            if tile_actionid in movement_aids:
                ground_item = tile_info.get("ground_item") or {}
                tele_dest = tile_info.get("tele_dest")
                entries.append(
                    TeleportEntry(
                        source_x=x,
                        source_y=y,
                        source_z=z,
                        itemid=ground_item.get("itemid"),
                        actionid=tile_actionid,
                        uniqueid=int(tile_info.get("uniqueid") or 0),
                        dest_x=tele_dest["x"] if tele_dest else None,
                        dest_y=tele_dest["y"] if tele_dest else None,
                        dest_z=tele_dest["z"] if tele_dest else None,
                        origin="tile",
                        teleport_kind="movement_action_teleport",
                        movement_script=movement_aids[tile_actionid],
                    )
                )

    entries.sort(key=lambda e: (e.source_z, e.source_x, e.source_y, e.itemid or 0, e.actionid, e.teleport_kind))
    return entries


def write_report(entries: list[TeleportEntry], output_base: Path) -> None:
    rows = [entry.to_row() for entry in entries]
    csv_path = output_base.with_suffix(".csv")
    json_path = output_base.with_suffix(".json")
    fieldnames = list(rows[0].keys()) if rows else [
        "source_x", "source_y", "source_z", "itemid", "actionid", "uniqueid",
        "dest_x", "dest_y", "dest_z", "origin", "teleport_kind", "movement_script",
    ]

    with csv_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    json_path.write_text(json.dumps(rows, indent=2, ensure_ascii=False), encoding="utf-8")


def parse_pos(text: str) -> tuple[int, int, int]:
    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 3:
        raise ValueError(f"invalid position {text!r}, expected x,y,z")
    return int(parts[0]), int(parts[1]), int(parts[2])


def save_map(otbm_path: Path, identifier: bytes, root: Node, backup: bool) -> Path | None:
    backup_path = None
    if backup:
        backup_path = otbm_path.with_suffix(otbm_path.suffix + ".bak")
        shutil.copy2(otbm_path, backup_path)
    otbm_path.write_bytes(identifier + serialize_node(root))
    return backup_path


def cmd_list(args: argparse.Namespace) -> int:
    teleport_itemids = load_teleport_itemids(args.items_xml)
    movement_aids = load_movement_teleport_aids(args.movements_xml)
    _, root = load_map_tree(args.map)
    entries = collect_entries(root, teleport_itemids, movement_aids)

    if args.output:
        write_report(entries, args.output)

    print(f"teleport item ids: {sorted(teleport_itemids)}")
    print(f"movement action ids: {sorted(movement_aids)}")
    print(f"entries: {len(entries)}")
    for entry in entries[: args.limit]:
        row = entry.to_row()
        print(json.dumps(row, ensure_ascii=False))
    return 0


def cmd_set(args: argparse.Namespace) -> int:
    source = parse_pos(args.source)
    dest = parse_pos(args.dest)
    teleport_itemids = load_teleport_itemids(args.items_xml)
    movement_aids = load_movement_teleport_aids(args.movements_xml)
    identifier, root = load_map_tree(args.map)
    entries = collect_entries(root, teleport_itemids, movement_aids)

    matches = [
        entry for entry in entries
        if entry.source_x == source[0]
        and entry.source_y == source[1]
        and entry.source_z == source[2]
        and (args.itemid is None or entry.itemid == args.itemid)
        and entry.teleport_kind == "item_type_teleport"
    ]

    if not matches:
        raise SystemExit(f"no map teleport item found at {source}")
    if len(matches) > 1:
        raise SystemExit(f"multiple teleport items found at {source}; use --itemid to disambiguate")

    entry = matches[0]
    if entry.item_node is None:
        raise SystemExit("selected entry is not an item node")
    if entry.dest_x is None:
        raise SystemExit("selected teleport has no embedded tele destination; it may be script-driven")

    entry.item_node.props = replace_item_teledest(entry.item_node.props, dest)
    backup_path = save_map(args.map, identifier, root, backup=not args.no_backup)

    print(
        f"updated teleport item {entry.itemid} at {source} "
        f"from ({entry.dest_x}, {entry.dest_y}, {entry.dest_z}) to {dest}"
    )
    if backup_path:
        print(f"backup: {backup_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="List or edit teleport destinations stored in an OTBM map.")
    parser.add_argument("--map", type=Path, default=Path(r"D:\tibia-oldschool\server\data\world\world.otbm"))
    parser.add_argument("--items-xml", type=Path, default=Path(r"D:\tibia-oldschool\server\data\items\items.xml"))
    parser.add_argument("--movements-xml", type=Path, default=Path(r"D:\tibia-oldschool\server\data\movements\movements.xml"))

    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List teleport items and movement teleports found in the map.")
    list_parser.add_argument("--limit", type=int, default=20)
    list_parser.add_argument("--output", type=Path, help="Base path for CSV/JSON export, without extension.")
    list_parser.set_defaults(func=cmd_list)

    set_parser = subparsers.add_parser("set", help="Set the destination of a map teleport item.")
    set_parser.add_argument("--source", required=True, help="Teleport source position as x,y,z")
    set_parser.add_argument("--dest", required=True, help="Destination position as x,y,z")
    set_parser.add_argument("--itemid", type=int, help="Optional item id to disambiguate source tile")
    set_parser.add_argument("--no-backup", action="store_true", help="Do not create .bak before writing")
    set_parser.set_defaults(func=cmd_set)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
