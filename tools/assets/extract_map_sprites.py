#!/usr/bin/env python3
import argparse
import csv
import json
import math
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from extract_sprites import read_u16, save_sheet
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


OTB_NODE_ESCAPE = 0xFD
OTB_NODE_START = 0xFE
OTB_NODE_END = 0xFF

ITEM_ATTR_SERVERID = 0x10
ITEM_ATTR_CLIENTID = 0x11

OTBM_TILE = 5
OTBM_ITEM = 6
OTBM_HOUSETILE = 14
OTBM_ATTR_TILE_FLAGS = 3
OTBM_ATTR_ITEM = 9


@dataclass
class ActiveNode:
    node_type: int
    props_begin: int
    props_end: int | None = None


class PropReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def remaining(self) -> int:
        return len(self.data) - self.offset

    def read_u8(self) -> int:
        if self.remaining() < 1:
            raise EOFError("Unable to read u8")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_u16(self) -> int:
        if self.remaining() < 2:
            raise EOFError("Unable to read u16")
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def read_u32(self) -> int:
        if self.remaining() < 4:
            raise EOFError("Unable to read u32")
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def skip(self, count: int) -> None:
        if self.remaining() < count:
            raise EOFError(f"Unable to skip {count} bytes")
        self.offset += count


def unescape_props(raw: bytes) -> bytes:
    out = bytearray()
    escaped = False
    for byte in raw:
        if byte == OTB_NODE_ESCAPE and not escaped:
            escaped = True
            continue
        out.append(byte)
        escaped = False
    return bytes(out)


def iter_otb_nodes(path: Path, accepted_identifier: bytes | None = None):
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path} is too small to be an OTB/OTBM file")

    identifier = data[:4]
    if accepted_identifier is not None and identifier not in (accepted_identifier, b"\x00\x00\x00\x00"):
        raise ValueError(f"{path} has unexpected identifier {identifier!r}")

    offset = 4
    if data[offset] != OTB_NODE_START:
        raise ValueError(f"{path} does not start with an OTB node")

    offset += 1
    stack = [ActiveNode(data[offset], offset + 1)]
    offset += 1

    while offset < len(data):
        byte = data[offset]

        if byte == OTB_NODE_START:
            if stack[-1].props_end is None:
                stack[-1].props_end = offset
            offset += 1
            if offset >= len(data):
                raise ValueError(f"{path} ended after node start marker")
            stack.append(ActiveNode(data[offset], offset + 1))
            offset += 1
            continue

        if byte == OTB_NODE_END:
            node = stack.pop()
            if node.props_end is None:
                node.props_end = offset
            props = unescape_props(data[node.props_begin:node.props_end])
            yield node.node_type, props
            offset += 1
            if not stack:
                break
            continue

        if byte == OTB_NODE_ESCAPE:
            offset += 2
            continue

        offset += 1

    if stack:
        raise ValueError(f"{path} ended before all OTB nodes were closed")


def read_otb_mapping(otb_path: Path) -> dict[int, int]:
    server_to_client: dict[int, int] = {}

    for _node_type, props in iter_otb_nodes(otb_path):
        if len(props) < 4:
            continue

        reader = PropReader(props)
        reader.skip(4)  # item flags

        server_id = 0
        client_id = 0
        while reader.remaining() > 0:
            attr = reader.read_u8()
            data_len = reader.read_u16()
            if attr == ITEM_ATTR_SERVERID:
                if data_len == 2:
                    server_id = reader.read_u16()
                else:
                    reader.skip(data_len)
            elif attr == ITEM_ATTR_CLIENTID:
                if data_len == 2:
                    client_id = reader.read_u16()
                else:
                    reader.skip(data_len)
            else:
                reader.skip(data_len)

        if server_id > 0 and client_id > 0:
            server_to_client[server_id] = client_id

    return server_to_client


def parse_otbm_root_version(otbm_path: Path) -> int:
    for node_type, props in iter_otb_nodes(otbm_path, b"OTBM"):
        if node_type != 0:
            continue
        if len(props) < 4:
            break
        return struct.unpack_from("<I", props, 0)[0]
    return 1


def collect_otbm_server_ids(otbm_path: Path) -> Counter[int]:
    counts: Counter[int] = Counter()

    for node_type, props in iter_otb_nodes(otbm_path, b"OTBM"):
        if node_type == OTBM_ITEM:
            if len(props) >= 2:
                counts[struct.unpack_from("<H", props, 0)[0]] += 1
            continue

        if node_type not in (OTBM_TILE, OTBM_HOUSETILE):
            continue

        reader = PropReader(props)
        try:
            reader.skip(2)  # tile local x/y
            if node_type == OTBM_HOUSETILE:
                reader.skip(4)  # house id

            while reader.remaining() > 0:
                attr = reader.read_u8()
                if attr == OTBM_ATTR_TILE_FLAGS:
                    reader.skip(4)
                elif attr == OTBM_ATTR_ITEM:
                    counts[reader.read_u16()] += 1
                else:
                    # Tile nodes in this server only use flags and compact ground item attrs.
                    break
        except EOFError:
            continue

    return counts


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


def write_csv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def contact_sheet(images: list[tuple[int, Image.Image]], output: Path, tile_size: int) -> None:
    if not images:
        return
    columns = 32
    rows = math.ceil(len(images) / columns)
    max_rows = 80
    limited = images[: columns * max_rows]
    save_sheet(limited, output, columns=columns, tile_size=tile_size)


class SpriteArchive:
    def __init__(self, spr_path: Path, sprite_size: int) -> None:
        self.raw = spr_path.read_bytes()
        self.sprite_size = sprite_size
        self.sprites_count = struct.unpack_from("<H", self.raw, 4)[0]
        self.addr_table = 6

    def decode(self, sprite_id: int) -> Image.Image | None:
        if sprite_id <= 0 or sprite_id > self.sprites_count:
            return None

        sprite_addr = struct.unpack_from("<I", self.raw, self.addr_table + (sprite_id - 1) * 4)[0]
        if sprite_addr == 0:
            return None

        cursor = sprite_addr + 3  # color key
        pixel_data_size, cursor = read_u16(self.raw, cursor)
        sprite_payload = self.raw[cursor: cursor + pixel_data_size]

        image = Image.new("RGBA", (self.sprite_size, self.sprite_size), (0, 0, 0, 0))
        pixels = image.load()

        payload_offset = 0
        pixel_index = 0
        total_pixels = self.sprite_size * self.sprite_size

        while payload_offset + 4 <= len(sprite_payload) and pixel_index < total_pixels:
            transparent_pixels, payload_offset = read_u16(sprite_payload, payload_offset)
            colored_pixels, payload_offset = read_u16(sprite_payload, payload_offset)
            pixel_index += transparent_pixels

            for _ in range(colored_pixels):
                if payload_offset + 3 > len(sprite_payload) or pixel_index >= total_pixels:
                    break

                x = pixel_index % self.sprite_size
                y = pixel_index // self.sprite_size
                r, g, b = struct.unpack_from("<BBB", sprite_payload, payload_offset)
                pixels[x, y] = (r, g, b, 255)
                payload_offset += 3
                pixel_index += 1

        return image


def extract_sprite_pngs(
    spr_path: Path,
    sprite_ids: list[int],
    out_dir: Path,
    sprite_size: int,
    sheet_path: Path | None,
) -> tuple[int, list[int]]:
    out_dir.mkdir(parents=True, exist_ok=True)

    archive = SpriteArchive(spr_path, sprite_size)
    extracted: list[tuple[int, Image.Image]] = []
    missing: list[int] = []
    for index, sprite_id in enumerate(sprite_ids, start=1):
        image = archive.decode(sprite_id)
        if image is None:
            missing.append(sprite_id)
            continue
        image.save(out_dir / f"{sprite_id}.png")
        extracted.append((sprite_id, image))
        if index % 500 == 0:
            print(f"[info] extracted {index}/{len(sprite_ids)} sprite ids")

    if sheet_path:
        contact_sheet(extracted, sheet_path, sprite_size)

    return len(extracted), missing


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract all item sprites used by an OTBM map through client ids.")
    parser.add_argument("--map", required=True, help="Path to world.otbm")
    parser.add_argument("--otb", required=True, help="Path to server items.otb")
    parser.add_argument("--dat", required=True, help="Path to client Tibia.dat")
    parser.add_argument("--spr", required=True, help="Path to client Tibia.spr")
    parser.add_argument("--out-root", required=True, help="Output root for metadata and extracted sprite PNGs")
    parser.add_argument("--exclude-client-ids", help="Comma-separated client ids or ranges to skip")
    parser.add_argument("--extract", action="store_true", help="Extract original 32x32 sprite PNGs")
    parser.add_argument("--clean-output", action="store_true", help="Remove old numeric PNGs before extracting")
    parser.add_argument("--sheet", action="store_true", help="Write a preview sheet for extracted sprites")
    args = parser.parse_args()

    map_path = Path(args.map)
    otb_path = Path(args.otb)
    dat_path = Path(args.dat)
    spr_path = Path(args.spr)
    out_root = Path(args.out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    print("[info] reading items.otb server->client mapping")
    server_to_client = read_otb_mapping(otb_path)
    print(f"[info] mapped {len(server_to_client)} server item ids")

    map_version = parse_otbm_root_version(map_path)
    print(f"[info] reading map ids from {map_path} (otbm version {map_version})")
    server_counts = collect_otbm_server_ids(map_path)
    print(f"[info] found {len(server_counts)} distinct server item ids in map")

    exclude_client_ids = parse_int_list(args.exclude_client_ids)
    missing_mapping: list[int] = []
    translated: dict[int, int] = {}
    for server_id in sorted(server_counts):
        client_id = server_to_client.get(server_id)
        if client_id is None:
            missing_mapping.append(server_id)
            continue
        if client_id in exclude_client_ids:
            continue
        translated[server_id] = client_id

    client_ids = sorted(set(translated.values()))
    print(f"[info] translated to {len(client_ids)} distinct client item ids")

    print("[info] parsing Tibia.dat for client sprite ids")
    things = parse_dat(dat_path, 772, THING_CATEGORY_ITEM, set(client_ids))
    missing_client_ids = [client_id for client_id in client_ids if client_id not in things]

    sprite_ids: list[int] = []
    seen_sprites: set[int] = set()
    client_rows: list[dict[str, object]] = []
    for client_id in client_ids:
        thing = things.get(client_id)
        if thing is None:
            continue
        for sprite_id in thing.unique_sprites:
            if sprite_id not in seen_sprites:
                sprite_ids.append(sprite_id)
                seen_sprites.add(sprite_id)
        client_rows.append(
            {
                "clientId": client_id,
                "width": thing.width,
                "height": thing.height,
                "layers": thing.layers,
                "patternX": thing.pattern_x,
                "patternY": thing.pattern_y,
                "patternZ": thing.pattern_z,
                "frames": thing.frames,
                "spriteCount": len(thing.unique_sprites),
                "sprites": " ".join(str(sprite_id) for sprite_id in thing.unique_sprites),
            }
        )

    server_rows = [
        {
            "serverId": server_id,
            "clientId": client_id,
            "mapOccurrences": server_counts[server_id],
        }
        for server_id, client_id in sorted(translated.items())
    ]

    write_csv(out_root / "map-server-to-client.csv", server_rows, ["serverId", "clientId", "mapOccurrences"])
    write_csv(
        out_root / "client-items-to-sprites.csv",
        client_rows,
        ["clientId", "width", "height", "layers", "patternX", "patternY", "patternZ", "frames", "spriteCount", "sprites"],
    )
    (out_root / "sprite-ids.txt").write_text(",".join(str(sprite_id) for sprite_id in sprite_ids), encoding="utf-8")

    summary = {
        "map": str(map_path),
        "otb": str(otb_path),
        "dat": str(dat_path),
        "spr": str(spr_path),
        "otbmVersion": map_version,
        "serverItemIdsInMap": len(server_counts),
        "translatedServerItemIds": len(translated),
        "clientItemIds": len(client_ids),
        "spriteIds": len(sprite_ids),
        "missingServerIdMappings": missing_mapping,
        "missingClientIdsInDat": missing_client_ids,
        "excludedClientIds": sorted(exclude_client_ids),
    }
    (out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"[ok] wrote metadata to {out_root}")
    print(f"[info] unique sprite ids: {len(sprite_ids)}")

    if args.extract:
        sprite_out = out_root / "sprites-32"
        if args.clean_output and sprite_out.exists():
            for png in sprite_out.glob("*.png"):
                if png.stem.isdigit():
                    png.unlink()

        sheet_path = out_root / "sprites-32-sheet.png" if args.sheet else None
        extracted, missing_sprites = extract_sprite_pngs(spr_path, sprite_ids, sprite_out, 32, sheet_path)
        summary["extractedSprites"] = extracted
        summary["missingSpriteIds"] = missing_sprites
        (out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"[ok] extracted {extracted} sprites to {sprite_out}")
        if missing_sprites:
            print(f"[warn] missing sprite ids: {len(missing_sprites)}")


if __name__ == "__main__":
    main()
