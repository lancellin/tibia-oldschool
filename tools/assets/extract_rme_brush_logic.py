#!/usr/bin/env python3
import argparse
import csv
import json
import struct
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


NODE_START = 0xFE
NODE_END = 0xFF
ESCAPE_CHAR = 0xFD

ITEM_ATTR_SERVERID = 0x10
ITEM_ATTR_CLIENTID = 0x11


@dataclass
class OTBNode:
    data: bytes
    children: list["OTBNode"]


def parse_otb_node(raw: bytes, pos: int) -> tuple[OTBNode, int]:
    if raw[pos] != NODE_START:
        raise ValueError(f"Expected OTB node start at {pos}, got 0x{raw[pos]:02x}")
    pos += 1

    data = bytearray()
    children: list[OTBNode] = []
    while pos < len(raw):
        byte = raw[pos]
        pos += 1
        if byte == ESCAPE_CHAR:
            if pos >= len(raw):
                raise ValueError("Unexpected OTB EOF after escape byte")
            data.append(raw[pos])
            pos += 1
        elif byte == NODE_START:
            pos -= 1
            while pos < len(raw) and raw[pos] == NODE_START:
                child, pos = parse_otb_node(raw, pos)
                children.append(child)
            if pos >= len(raw) or raw[pos] != NODE_END:
                raise ValueError(f"Expected OTB child-tree end at {pos}")
            pos += 1
            return OTBNode(bytes(data), children), pos
        elif byte == NODE_END:
            return OTBNode(bytes(data), children), pos
        else:
            data.append(byte)
    raise ValueError("Unexpected OTB EOF while reading node")


def read_u8(data: bytes, offset: int) -> tuple[int, int]:
    return data[offset], offset + 1


def read_u16(data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<H", data, offset)[0], offset + 2


def parse_items_otb(path: Path) -> dict[int, int]:
    raw = path.read_bytes()
    if raw[:4] != b"OTBI" and raw[:4] != b"\x00\x00\x00\x00":
        raise ValueError(f"{path} is not an OTBI file")

    root, _ = parse_otb_node(raw, 4)
    mapping: dict[int, int] = {}

    for node in root.children:
        data = node.data
        if len(data) < 5:
            continue
        offset = 1 + 4  # item group + flags
        server_id = None
        client_id = None
        while offset + 3 <= len(data):
            attr, offset = read_u8(data, offset)
            size, offset = read_u16(data, offset)
            payload_start = offset
            payload_end = payload_start + size
            if payload_end > len(data):
                break
            if attr == ITEM_ATTR_SERVERID and size == 2:
                server_id = struct.unpack_from("<H", data, payload_start)[0]
            elif attr == ITEM_ATTR_CLIENTID and size == 2:
                client_id = struct.unpack_from("<H", data, payload_start)[0]
            offset = payload_end

        if server_id is not None and client_id is not None:
            mapping[server_id] = client_id

    return mapping


def expand_id_attrs(element: ET.Element, id_attr: str = "id") -> list[int]:
    if id_attr in element.attrib:
        return [int(element.attrib[id_attr])]
    if "fromid" in element.attrib:
        start = int(element.attrib["fromid"])
        end = int(element.attrib.get("toid", start))
        return list(range(start, end + 1))
    return []


def parse_borders(data_dir: Path, server_to_client: dict[int, int]) -> dict[str, dict[str, object]]:
    borders: dict[str, dict[str, object]] = {}
    path = data_dir / "borders.xml"
    if not path.exists():
        return borders

    root = ET.parse(path).getroot()
    for border in root.findall("border"):
        border_id = border.attrib.get("id")
        if border_id is None:
            continue
        items = []
        for border_item in border.findall("borderitem"):
            server_id = int(border_item.attrib["item"])
            items.append({
                "edge": border_item.attrib.get("edge"),
                "serverId": server_id,
                "clientId": server_to_client.get(server_id),
            })
        borders[border_id] = {
            "id": int(border_id),
            "group": int(border.attrib["group"]) if "group" in border.attrib else None,
            "items": items,
        }
    return borders


def load_material_brushes(data_dir: Path) -> dict[str, list[dict[str, object]]]:
    brushes: dict[str, list[dict[str, object]]] = {}
    for filename in ("grounds.xml", "walls.xml", "doodads.xml"):
        path = data_dir / filename
        if not path.exists():
            continue
        root = ET.parse(path).getroot()
        for brush in root.findall("brush"):
            name = brush.attrib.get("name")
            if not name:
                continue
            brushes.setdefault(name, []).append({"source": filename, "node": brush})
    return brushes


def choose_brush(candidates: list[dict[str, object]]) -> dict[str, object]:
    return max(candidates, key=lambda item: len(list(item["node"].iter("item"))))


def collect_item_refs(node: ET.Element, server_to_client: dict[int, int]) -> list[dict[str, object]]:
    refs = []
    for element in node.iter():
        if element.tag == "item":
            for server_id in expand_id_attrs(element):
                refs.append({
                    "kind": "item",
                    "serverId": server_id,
                    "clientId": server_to_client.get(server_id),
                    "chance": int(element.attrib["chance"]) if "chance" in element.attrib else None,
                })
        elif element.tag == "door" and "id" in element.attrib:
            server_id = int(element.attrib["id"])
            refs.append({
                "kind": "door",
                "serverId": server_id,
                "clientId": server_to_client.get(server_id),
                "doorType": element.attrib.get("type"),
                "open": element.attrib.get("open"),
            })
    return refs


def collect_composites(node: ET.Element, server_to_client: dict[int, int]) -> list[dict[str, object]]:
    composites = []
    for alternate_index, alternate in enumerate(node.findall("alternate")):
        for composite_index, composite in enumerate(alternate.findall("composite")):
            tiles = []
            for tile in composite.findall("tile"):
                tile_pos = {
                    "x": int(tile.attrib.get("x", 0)),
                    "y": int(tile.attrib.get("y", 0)),
                    "z": int(tile.attrib.get("z", 0)),
                }
                for item in tile.findall("item"):
                    for server_id in expand_id_attrs(item):
                        tiles.append({
                            "pos": tile_pos,
                            "serverId": server_id,
                            "clientId": server_to_client.get(server_id),
                        })
            composites.append({
                "alternateIndex": alternate_index,
                "compositeIndex": composite_index,
                "chance": int(composite.attrib["chance"]) if "chance" in composite.attrib else None,
                "tiles": tiles,
            })
    return composites


def resolve_brush(
    name: str,
    category: str,
    material_brushes: dict[str, list[dict[str, object]]],
    borders: dict[str, dict[str, object]],
    server_to_client: dict[int, int],
) -> dict[str, object]:
    if name not in material_brushes:
        return {"name": name, "paletteCategory": category, "missing": True}

    entry = choose_brush(material_brushes[name])
    node = entry["node"]
    border_refs = []
    for border in node.findall("border"):
        border_id = border.attrib.get("id")
        if border_id is None:
            continue
        border_refs.append({
            "id": int(border_id),
            "align": border.attrib.get("align"),
            "to": border.attrib.get("to"),
            "items": borders.get(border_id, {}).get("items", []),
        })

    return {
        "name": name,
        "paletteCategory": category,
        "source": entry["source"],
        "type": node.attrib.get("type"),
        "serverLookId": int(node.attrib["server_lookid"]) if "server_lookid" in node.attrib else None,
        "lookId": int(node.attrib["lookid"]) if "lookid" in node.attrib else None,
        "serverLookClientId": server_to_client.get(int(node.attrib["server_lookid"])) if "server_lookid" in node.attrib else None,
        "lookClientId": server_to_client.get(int(node.attrib["lookid"])) if "lookid" in node.attrib else None,
        "items": collect_item_refs(node, server_to_client),
        "borders": border_refs,
        "friends": [friend.attrib.get("name") for friend in node.findall("friend") if friend.attrib.get("name")],
        "composites": collect_composites(node, server_to_client),
    }


def parse_tileset(path: Path, tileset_name: str) -> list[tuple[str, str]]:
    root = ET.parse(path).getroot()
    for tileset in root.findall("tileset"):
        if tileset.attrib.get("name") != tileset_name:
            continue
        refs: list[tuple[str, str]] = []
        for category in tileset:
            for child in category:
                if child.tag == "brush" and "name" in child.attrib:
                    refs.append((category.tag, child.attrib["name"]))
        return refs
    raise ValueError(f"Tileset not found: {tileset_name}")


def write_flat_csv(path: Path, brushes: list[dict[str, object]]) -> None:
    seen: set[tuple[str, int, int | None, str]] = set()
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["brush", "source", "type", "kind", "serverId", "clientId", "context"])
        writer.writeheader()
        for brush in brushes:
            for item in brush.get("items", []):
                key = (brush["name"], item["serverId"], item.get("clientId"), item["kind"])
                if key in seen:
                    continue
                seen.add(key)
                writer.writerow({
                    "brush": brush["name"],
                    "source": brush.get("source"),
                    "type": brush.get("type"),
                    "kind": item["kind"],
                    "serverId": item["serverId"],
                    "clientId": item.get("clientId"),
                    "context": item.get("doorType") or "",
                })
            for border in brush.get("borders", []):
                for item in border.get("items", []):
                    key = (brush["name"], item["serverId"], item.get("clientId"), f"border-{border['id']}")
                    if key in seen:
                        continue
                    seen.add(key)
                    writer.writerow({
                        "brush": brush["name"],
                        "source": brush.get("source"),
                        "type": brush.get("type"),
                        "kind": "border",
                        "serverId": item["serverId"],
                        "clientId": item.get("clientId"),
                        "context": f"border {border['id']} {item.get('edge') or ''}".strip(),
                    })


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract RME tileset/brush logic and map server ids to client ids.")
    parser.add_argument("--rme-root", type=Path, default=Path(r"C:\tibia-oldschool\sources\rme-otacademy"))
    parser.add_argument("--version", default="772")
    parser.add_argument("--tileset", default="Town")
    parser.add_argument("--brush", action="append", help="Resolve one brush by name. Can be passed multiple times.")
    parser.add_argument("--out-root", type=Path, default=Path(r"C:\tibia-oldschool\tools\assets\tests\rme-brush-logic-772"))
    args = parser.parse_args()

    data_dir = args.rme_root / "data" / args.version
    server_to_client = parse_items_otb(data_dir / "items.otb")
    borders = parse_borders(data_dir, server_to_client)
    material_brushes = load_material_brushes(data_dir)

    if args.brush:
        refs = [("manual", name) for name in args.brush]
        output_name = "brushes"
    else:
        refs = parse_tileset(data_dir / "tilesets.xml", args.tileset)
        output_name = f"tileset-{args.tileset.lower().replace(' ', '-')}"

    resolved = [resolve_brush(name, category, material_brushes, borders, server_to_client) for category, name in refs]
    args.out_root.mkdir(parents=True, exist_ok=True)

    json_path = args.out_root / f"{output_name}.json"
    csv_path = args.out_root / f"{output_name}-flat-ids.csv"
    json_path.write_text(json.dumps(resolved, indent=2), encoding="utf-8")
    write_flat_csv(csv_path, resolved)

    missing = [brush["name"] for brush in resolved if brush.get("missing")]
    print(json.dumps({
        "version": args.version,
        "tileset": None if args.brush else args.tileset,
        "brushCount": len(resolved),
        "missing": missing,
        "json": str(json_path),
        "csv": str(csv_path),
        "serverToClientCount": len(server_to_client),
    }, indent=2))


if __name__ == "__main__":
    main()
