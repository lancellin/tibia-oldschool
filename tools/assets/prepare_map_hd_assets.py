#!/usr/bin/env python3
import argparse
import csv
import json
import shutil
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from extract_map_sprites import collect_otbm_server_ids, parse_otbm_root_version, read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, extract_assets, parse_dat


PORTABLE_ATTRS = {
    "weight",
    "containersize",
    "weapontype",
    "slottype",
    "ammotype",
    "shoottype",
    "range",
    "attack",
    "defense",
    "extradef",
    "armor",
    "charges",
    "runespellname",
    "magiclevelpoints",
    "skillsword",
    "skillaxe",
    "skillclub",
    "skilldist",
    "skillfish",
    "skillshield",
    "mana",
    "health",
}

TRANSIENT_ATTRS = {
    "corpsetype",
}

TRANSIENT_NAME_PARTS = {
    "blood",
    "blood splatter",
    "pool of blood",
    "pool of slime",
    "dead ",
    "corpse",
    "remains",
    "magic wall",
    "wild growth",
    "poison field",
    "fire field",
    "energy field",
}

REPEATING_GROUND_ALLOW = {
    "cobbled",
    "dirt",
    "earth",
    "floor",
    "flowers",
    "grass",
    "gravel",
    "ice",
    "lava",
    "mud",
    "pavement",
    "roof",
    "sand",
    "snow",
    "soil",
    "swamp",
    "tile",
    "void",
    "water",
}

REPEATING_GROUND_BLOCK = {
    "banner",
    "bed",
    "book",
    "box",
    "bridge",
    "chair",
    "chest",
    "coal basin",
    "container",
    "door",
    "flag",
    "fence",
    "hole",
    "ladder",
    "lamp",
    "mail",
    "mailbox",
    "post",
    "rail",
    "ramp",
    "sign",
    "stair",
    "statue",
    "table",
    "torch",
    "wall",
    "window",
}


@dataclass
class XmlItem:
    item_id: int
    name: str
    article: str
    editor_suffix: str
    attrs: dict[str, str]


def parse_int_list(spec: str | None) -> set[int]:
    if not spec:
        return set()

    result: set[int] = set()
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            start_s, end_s = chunk.split("-", 1)
            start = int(start_s)
            end = int(end_s)
            step = 1 if end >= start else -1
            result.update(range(start, end + step, step))
        else:
            result.add(int(chunk))
    return result


def parse_items_xml(path: Path) -> dict[int, XmlItem]:
    tree = ET.parse(path)
    root = tree.getroot()
    items: dict[int, XmlItem] = {}

    for item_node in root.findall("item"):
        raw_ids: list[int] = []
        if "id" in item_node.attrib:
            raw_ids.append(int(item_node.attrib["id"]))
        elif "fromid" in item_node.attrib and "toid" in item_node.attrib:
            raw_ids.extend(range(int(item_node.attrib["fromid"]), int(item_node.attrib["toid"]) + 1))
        if not raw_ids:
            continue

        attrs: dict[str, str] = {}
        for attr_node in item_node.findall("attribute"):
            key = attr_node.attrib.get("key", "").strip().lower()
            value = attr_node.attrib.get("value", "")
            if key:
                attrs[key] = value

        for item_id in raw_ids:
            items[item_id] = XmlItem(
                item_id=item_id,
                name=item_node.attrib.get("name", ""),
                article=item_node.attrib.get("article", ""),
                editor_suffix=item_node.attrib.get("editorsuffix", ""),
                attrs=attrs,
            )

    return items


def exclusion_reason(item: XmlItem | None) -> str | None:
    if item is None:
        return None

    attr_keys = set(item.attrs)
    portable_hits = sorted(PORTABLE_ATTRS & attr_keys)
    if portable_hits:
        return "portable_attr:" + ",".join(portable_hits)

    transient_hits = sorted(TRANSIENT_ATTRS & attr_keys)
    if transient_hits:
        return "transient_attr:" + ",".join(transient_hits)

    normalized_name = item.name.lower()
    for marker in sorted(TRANSIENT_NAME_PARTS):
        if marker in normalized_name:
            return "transient_name:" + marker

    if "decayto" in attr_keys and "floorchange" not in attr_keys:
        return "transient_attr:decayto"

    return None


def is_repeating_ground_item(item: XmlItem | None) -> bool:
    if item is None:
        return False

    name = item.name.lower()
    if any(marker in name for marker in REPEATING_GROUND_BLOCK):
        return False

    if any(marker in name for marker in REPEATING_GROUND_ALLOW):
        return True

    # Most 7.72 raw terrain ids have no article and no gameplay attrs.
    return not item.article.strip() and not item.attrs


def write_csv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def flatten_upscayl_inputs(original_root: Path, flat_root: Path, mosaic_client_ids: set[int]) -> list[dict[str, object]]:
    index_path = original_root / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8"))
    flat_root.mkdir(parents=True, exist_ok=True)

    for png in flat_root.glob("*.png"):
        png.unlink()

    rows: list[dict[str, object]] = []
    for entry in index:
        client_id = int(entry["clientId"])
        folder = Path(entry["folder"])
        metadata_path = folder / "metadata.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        is_ground = bool(metadata.get("isGround", False))
        mosaic = folder / "mosaic-input.png"
        sheet = folder / "sheet.png"

        if is_ground and client_id in mosaic_client_ids and mosaic.exists():
            source = mosaic
            kind = "mosaic"
            target = flat_root / f"item-{client_id}-{kind}.png"
            shutil.copy2(source, target)
            rows.append(
                {
                    "clientId": client_id,
                    "spriteId": "",
                    "kind": kind,
                    "source": str(source),
                    "input": str(target),
                }
            )
            continue

        if sheet.exists():
            source = sheet
            kind = "sheet"
            target = flat_root / f"item-{client_id}-{kind}.png"
            shutil.copy2(source, target)
            rows.append(
                {
                    "clientId": client_id,
                    "spriteId": "",
                    "kind": kind,
                    "source": str(source),
                    "input": str(target),
                }
            )

    return rows


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare map/scenery item assets for HD sprite upscaling while excluding portable items and outfits."
    )
    parser.add_argument("--map", required=True, help="Path to world.otbm")
    parser.add_argument("--otb", required=True, help="Path to server items.otb")
    parser.add_argument("--items-xml", required=True, help="Path to server items.xml")
    parser.add_argument("--dat", required=True, help="Path to client Tibia.dat")
    parser.add_argument("--spr", required=True, help="Path to client Tibia.spr")
    parser.add_argument("--out-root", required=True, help="Output root for this batch")
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--force-include-server-ids", help="Comma-separated server item ids/ranges to include anyway")
    parser.add_argument("--force-exclude-server-ids", help="Comma-separated server item ids/ranges to exclude")
    parser.add_argument("--sample-client-ids", help="Comma-separated client item ids/ranges to keep for a small validation batch")
    parser.add_argument("--sample-limit", type=int, help="Keep only the first N included client item ids for a small validation batch")
    parser.add_argument("--reuse-original", action="store_true", help="Reuse existing original/index.json and only rebuild inputs-flat/metadata")
    args = parser.parse_args()

    map_path = Path(args.map)
    otb_path = Path(args.otb)
    items_xml_path = Path(args.items_xml)
    dat_path = Path(args.dat)
    spr_path = Path(args.spr)
    out_root = Path(args.out_root)
    original_root = out_root / "original"
    flat_root = out_root / "inputs-flat"
    out_root.mkdir(parents=True, exist_ok=True)

    force_include_server_ids = parse_int_list(args.force_include_server_ids)
    force_exclude_server_ids = parse_int_list(args.force_exclude_server_ids)
    sample_client_ids = parse_int_list(args.sample_client_ids)

    server_to_client = read_otb_mapping(otb_path)
    map_version = parse_otbm_root_version(map_path)
    server_counts = collect_otbm_server_ids(map_path)
    xml_items = parse_items_xml(items_xml_path)

    included_server_rows: list[dict[str, object]] = []
    excluded_server_rows: list[dict[str, object]] = []
    client_to_server_ids: dict[int, set[int]] = {}
    mosaic_client_ids: set[int] = set()

    for server_id in sorted(server_counts):
        client_id = server_to_client.get(server_id)
        item = xml_items.get(server_id)
        reason = exclusion_reason(item)

        if server_id in force_include_server_ids:
            reason = None
        if server_id in force_exclude_server_ids:
            reason = "forced_exclude"
        if client_id is None:
            reason = "missing_otb_mapping"

        row = {
            "serverId": server_id,
            "clientId": client_id or "",
            "mapOccurrences": server_counts[server_id],
            "name": item.name if item else "",
            "article": item.article if item else "",
            "editorSuffix": item.editor_suffix if item else "",
            "attrs": " ".join(sorted(item.attrs)) if item else "",
            "reason": reason or "included",
        }

        if reason:
            excluded_server_rows.append(row)
            continue

        included_server_rows.append(row)
        resolved_client_id = int(client_id)
        client_to_server_ids.setdefault(resolved_client_id, set()).add(server_id)
        if is_repeating_ground_item(item):
            mosaic_client_ids.add(resolved_client_id)

    client_ids = sorted(client_to_server_ids)
    if sample_client_ids:
        client_ids = [client_id for client_id in client_ids if client_id in sample_client_ids]
        client_to_server_ids = {client_id: client_to_server_ids[client_id] for client_id in client_ids}
        mosaic_client_ids &= set(client_ids)
    if args.sample_limit is not None:
        if args.sample_limit <= 0:
            raise ValueError("--sample-limit must be greater than zero")
        client_ids = client_ids[: args.sample_limit]
        client_to_server_ids = {client_id: client_to_server_ids[client_id] for client_id in client_ids}
        mosaic_client_ids &= set(client_ids)
    things = parse_dat(dat_path, args.version, THING_CATEGORY_ITEM, set(client_ids))
    missing_client_ids = [client_id for client_id in client_ids if client_id not in things]
    client_ids = [client_id for client_id in client_ids if client_id in things]

    if not args.reuse_original and original_root.exists():
        for child in original_root.iterdir():
            if child.is_dir() and child.name.startswith("item-"):
                shutil.rmtree(child)
            elif child.name == "index.json":
                child.unlink()

    if args.reuse_original and not (original_root / "index.json").exists():
        raise FileNotFoundError(f"Cannot reuse original assets: missing {original_root / 'index.json'}")

    if not args.reuse_original:
        extract_assets(dat_path, spr_path, args.version, THING_CATEGORY_ITEM, client_ids, original_root)
    flat_rows = flatten_upscayl_inputs(original_root, flat_root, mosaic_client_ids)

    write_csv(
        out_root / "included-server-items.csv",
        included_server_rows,
        ["serverId", "clientId", "mapOccurrences", "name", "article", "editorSuffix", "attrs", "reason"],
    )
    write_csv(
        out_root / "excluded-server-items.csv",
        excluded_server_rows,
        ["serverId", "clientId", "mapOccurrences", "name", "article", "editorSuffix", "attrs", "reason"],
    )
    write_csv(out_root / "upscayl-inputs.csv", flat_rows, ["clientId", "spriteId", "kind", "source", "input"])
    (out_root / "mosaic-client-ids.txt").write_text(
        ",".join(str(client_id) for client_id in sorted(mosaic_client_ids)),
        encoding="utf-8",
    )

    summary = {
        "map": str(map_path),
        "otb": str(otb_path),
        "itemsXml": str(items_xml_path),
        "dat": str(dat_path),
        "spr": str(spr_path),
        "otbmVersion": map_version,
        "mapServerItemIds": len(server_counts),
        "includedServerItemIds": len(included_server_rows),
        "excludedServerItemIds": len(excluded_server_rows),
        "includedClientItemIds": len(client_ids),
        "mosaicClientItemIds": len(mosaic_client_ids),
        "sampleClientIds": sorted(sample_client_ids),
        "sampleLimit": args.sample_limit,
        "missingClientIdsInDat": missing_client_ids,
        "upscaylInputImages": len(flat_rows),
        "originalRoot": str(original_root),
        "upscaylInputRoot": str(flat_root),
        "notes": [
            "Only item-category map/scenery assets are included.",
            "Creature/outfit/effect/missile categories are not included.",
            "Portable items are excluded primarily by items.xml gameplay attributes such as weight/container/weapon/slot.",
            "Patterned 1x1 items get mosaic inputs to reduce visible seams after upscale.",
        ],
    }
    (out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"[ok] wrote batch to {out_root}")
    print(f"[info] included server ids: {len(included_server_rows)}")
    print(f"[info] included client ids: {len(client_ids)}")
    print(f"[info] upscayl input images: {len(flat_rows)}")
    print(f"[info] input folder: {flat_root}")


if __name__ == "__main__":
    main()
