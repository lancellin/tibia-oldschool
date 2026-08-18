#!/usr/bin/env python3
"""Replaces the discontinued elite portal item with two tier portals.

Phase 1 (uninstall): removes the old dedicated portal (server 26402 /
client 5119, 4 frames) appended by install_elite_portal.py, restoring the
crystals-only DAT/SPR/OTB state.

Phase 2 (install): appends
  - lightning portal: server 26402 -> client 5119 (elite tier 2)
  - infernal portal:  server 26403 -> client 5120 (elite tier 3)
each with 5 ping-pong animation frames over 3 unique sprites and the onTop
DAT attribute (drawn above corpses/creatures). Elite tier 1 keeps using the
vanilla magic forcefield (server 1387 / client 1949), untouched.

Run with --verify-installed for a read-only check of the final state.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from patch_classic_spr_cross_version import read_spr, write_spr
from patch_rebased_water_borders import encode_sprite
from patch_water_dat_spr_exact import scan_dat_records

OTB_NODE_ESCAPE = 0xFD
OTB_NODE_START = 0xFE
OTB_NODE_END = 0xFF
ITEM_ATTR_SERVERID = 0x10
ITEM_ATTR_CLIENTID = 0x11

DAT_ATTR_ON_TOP = 3
DAT_ATTR_TERMINATOR = 0xFF
FRAMES_PING_PONG = 5

OLD_SERVER_ID = 26402
OLD_CLIENT_ID = 5119
OLD_SPRITE_IDS = [16220, 16221, 16222, 16223]

LIGHTNING_SERVER_ID = 26402
LIGHTNING_CLIENT_ID = 5119
INFERNAL_SERVER_ID = 26403
INFERNAL_CLIENT_ID = 5120

SPR_COUNT_WITH_OLD_PORTAL = 16223
SPR_COUNT_CRYSTALS_ONLY = 16219
SPR_COUNT_FINAL = 16225
DAT_LAST_ITEM_CRYSTALS_ONLY = 5118
DAT_LAST_ITEM_FINAL = 5120

# State produced by install_elite_portal.py (old portal present).
PRE_UNINSTALL_SHA256 = {
    "sources/otclient-redemption/data/things/772/Tibia.dat": "71229492F71871FA2296A2C5D67747D604D83ABB6954EF42B67F835B4A6A7EF3",
    "sources/otclient-redemption/data/things/772/Tibia.spr": "E23C1FA28BEADF5ED54E2FC57CEE6190607818B111D534E5E022D449D7E2F6C3",
    "server/data/items/items.otb": "5EE4FD145905E52C248E3179EE0AD391FE575EF19B7E93D151A682F6D620D698",
    "sources/nekiro-tfs-1.5-7.72/data/items/items.otb": "5EE4FD145905E52C248E3179EE0AD391FE575EF19B7E93D151A682F6D620D698",
}

# Crystals-only state (what phase 1 must restore).
CRYSTALS_ONLY_SHA256 = {
    "dat": "0B62D4A4F223FBB293454CB539DE5CC542C2169915911EE7676925F785C82837",
    "spr": "B7DF319966BD29414D8271E520B7FCE06299FB231D202341E9A12E462D5324AC",
    "otb": "2E45D4E7DD9B1046825C102B9F00ED4A2D98AAE6DD4D9E69F05C9AC3176043DB",
}

ASSET_SHA256 = {
    "lightning_portal/197.png": "0690155031B3946529F6A3C297CD6BDD270CC612AA450312083323BBECDEFC6E",
    "lightning_portal/198.png": "31C9396756D352F30EF7B3B8E3863599EFFB8071F6124F60D0B5937547B929EA",
    "lightning_portal/199.png": "2DED785D03A13D8109240B5FB1502B94AF79B950EF19B45A12B5D092866668A0",
    "infernal_portal/197.png": "62DA3F71020E3923E323A107319DD2480B1BDA87258FB68B3AD8AD565D166B8D",
    "infernal_portal/198.png": "BD616AF5A943A7C1D4A6EB733B392D450089C70448A27D25900EE780E1EC8AC1",
    "infernal_portal/199.png": "BE8C868913B7A7F536C07931DDE3E835DCE0F227BC8D68C4768FA1CC5F04C9D6",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def verify_hashes(repo: Path, expected: dict) -> None:
    for relative, want in expected.items():
        got = sha256(repo / relative)
        if got != want:
            raise ValueError(f"Unexpected SHA-256 for {relative}: {got}; expected {want}")


def escape_otb_properties(properties: bytes) -> bytes:
    output = bytearray()
    for value in properties:
        if value in (OTB_NODE_ESCAPE, OTB_NODE_START, OTB_NODE_END):
            output.append(OTB_NODE_ESCAPE)
        output.append(value)
    return bytes(output)


def otb_node(server_id: int, client_id: int) -> bytes:
    properties = struct.pack(
        "<IBHHBHH", 0, ITEM_ATTR_SERVERID, 2, server_id, ITEM_ATTR_CLIENTID, 2, client_id
    )
    return bytes((OTB_NODE_START, 0)) + escape_otb_properties(properties) + bytes((OTB_NODE_END,))


def load_unique_sprites(assets_root: Path, folder: str) -> list:
    frames = []
    for index in (197, 198, 199):
        with Image.open(assets_root / folder / f"{index}.png") as image:
            rgba = image.convert("RGBA")
            if rgba.size != (32, 32):
                raise ValueError(f"{folder}/{index}.png is not 32x32")
            frames.append(rgba.copy())
    return frames


def ping_pong(sprite_ids: list) -> list:
    return [sprite_ids[0], sprite_ids[1], sprite_ids[2], sprite_ids[1], sprite_ids[0]]


def uninstall_old_portal(dat: Path, spr: Path, otb: Path) -> None:
    # DAT: drop the 5119 record and restore the item count header.
    data = bytearray(dat.read_bytes())
    _, records = scan_dat_records(bytes(data), 772)
    old = next((r for r in records if r.category == THING_CATEGORY_ITEM and r.client_id == OLD_CLIENT_ID), None)
    if old is not None:
        del data[old.start:old.end]
        struct.pack_into("<H", data, 4, DAT_LAST_ITEM_CRYSTALS_ONLY)
        dat.write_bytes(bytes(data))
    if sha256(dat) != CRYSTALS_ONLY_SHA256["dat"]:
        raise ValueError("DAT did not return to the crystals-only state")

    # SPR: drop the 4 appended sprites.
    signature, count, sprites = read_spr(spr)
    if count == SPR_COUNT_WITH_OLD_PORTAL:
        for sprite_id in OLD_SPRITE_IDS:
            sprites.pop(sprite_id, None)
        write_spr(spr, signature, SPR_COUNT_CRYSTALS_ONLY, sprites)
    if sha256(spr) != CRYSTALS_ONLY_SHA256["spr"]:
        raise ValueError("SPR did not return to the crystals-only state")

    # OTB: drop the trailing old-portal node.
    data = otb.read_bytes()
    node = otb_node(OLD_SERVER_ID, OLD_CLIENT_ID)
    if data.endswith(node + bytes((OTB_NODE_END,))):
        otb.write_bytes(data[: -len(node) - 1] + bytes((OTB_NODE_END,)))
    if sha256(otb) != CRYSTALS_ONLY_SHA256["otb"]:
        raise ValueError("OTB did not return to the crystals-only state")


def build_spr(input_spr: Path, output_spr: Path, assets_root: Path) -> dict:
    signature, count, sprites = read_spr(input_spr)
    if count != SPR_COUNT_CRYSTALS_ONLY:
        raise ValueError(f"Expected SPR to end at {SPR_COUNT_CRYSTALS_ONLY}, found {count}")

    sprite_map = {}
    next_id = count + 1
    for folder in ("lightning_portal", "infernal_portal"):
        ids = []
        for frame in load_unique_sprites(assets_root, folder):
            sprites[next_id] = encode_sprite(frame)
            ids.append(next_id)
            next_id += 1
        sprite_map[folder] = ids

    write_spr(output_spr, signature, next_id - 1, sprites)
    return sprite_map


def dat_record(client_id: int, sprite_ids: list) -> bytes:
    record = bytearray((DAT_ATTR_ON_TOP, DAT_ATTR_TERMINATOR))
    record.extend(bytes((1, 1, 1, 1, 1, 1, FRAMES_PING_PONG)))
    for sprite_id in ping_pong(sprite_ids):
        record.extend(struct.pack("<H", sprite_id))
    return bytes(record)


def build_dat(input_dat: Path, output_dat: Path, sprite_map: dict) -> None:
    data = input_dat.read_bytes()
    item_last, outfit_last, effect_last, missile_last = struct.unpack_from("<4H", data, 4)
    if item_last != DAT_LAST_ITEM_CRYSTALS_ONLY:
        raise ValueError(f"Expected DAT to end at {DAT_LAST_ITEM_CRYSTALS_ONLY}, found {item_last}")

    _, records = scan_dat_records(data, 772)
    present = {r.client_id for r in records if r.category == THING_CATEGORY_ITEM}
    if {LIGHTNING_CLIENT_ID, INFERNAL_CLIENT_ID} & present:
        raise ValueError("Tier portal client ids already present in the DAT")

    first_outfit = next(r for r in records if r.category == 1)
    appended = dat_record(LIGHTNING_CLIENT_ID, sprite_map["lightning_portal"])
    appended += dat_record(INFERNAL_CLIENT_ID, sprite_map["infernal_portal"])

    output = bytearray(data[:first_outfit.start])
    output.extend(appended)
    output.extend(data[first_outfit.start:])
    struct.pack_into("<4H", output, 4, DAT_LAST_ITEM_FINAL, outfit_last, effect_last, missile_last)
    output_dat.write_bytes(bytes(output))


def build_otb(input_otb: Path, output_otb: Path) -> None:
    mapping = read_otb_mapping(input_otb)
    if max(mapping) != 26401 or max(mapping.values()) != 5118:
        raise ValueError("Expected OTB mapping to end at server 26401 / client 5118")

    data = input_otb.read_bytes()
    if not data or data[-1] != OTB_NODE_END:
        raise ValueError(f"{input_otb} does not end with the root OTB node terminator")

    nodes = otb_node(LIGHTNING_SERVER_ID, LIGHTNING_CLIENT_ID)
    nodes += otb_node(INFERNAL_SERVER_ID, INFERNAL_CLIENT_ID)
    output_otb.write_bytes(data[:-1] + nodes + bytes((OTB_NODE_END,)))


def verify_outputs(dat: Path, spr: Path, otb_paths: list, sprite_map: dict) -> None:
    _, count, sprites = read_spr(spr)
    if count != SPR_COUNT_FINAL:
        raise ValueError(f"Expected {SPR_COUNT_FINAL} sprites, found {count}")

    things = parse_dat(dat, 772, THING_CATEGORY_ITEM, {LIGHTNING_CLIENT_ID, INFERNAL_CLIENT_ID, 1949})
    expected = {
        LIGHTNING_CLIENT_ID: sprite_map["lightning_portal"],
        INFERNAL_CLIENT_ID: sprite_map["infernal_portal"],
    }
    for client_id, unique_ids in expected.items():
        thing = things[client_id]
        if (thing.width, thing.height, thing.layers, thing.pattern_x, thing.pattern_y, thing.pattern_z, thing.frames) != (1, 1, 1, 1, 1, 1, FRAMES_PING_PONG):
            raise ValueError(f"Unexpected DAT layout for client item {client_id}")
        if thing.sprites != ping_pong(unique_ids):
            raise ValueError(f"Unexpected sprite order for client item {client_id}")
        if any(s not in sprites for s in thing.sprites):
            raise ValueError(f"Missing SPR payload for client item {client_id}")
    # 1949 carries OnTop (patch_portal_ontop.py) so the tier 1 portal also
    # renders above corpses/creatures.
    if 3 not in things[1949].attrs:
        raise ValueError("Client item 1949 must carry OnTop")

    data = dat.read_bytes()
    _, records = scan_dat_records(data, 772)
    for client_id in (LIGHTNING_CLIENT_ID, INFERNAL_CLIENT_ID):
        record = next(r for r in records if r.category == THING_CATEGORY_ITEM and r.client_id == client_id)
        attrs = data[record.start:record.geometry_start]
        if list(attrs) != [DAT_ATTR_ON_TOP, DAT_ATTR_TERMINATOR]:
            raise ValueError(f"Unexpected attrs for client item {client_id}: {list(attrs)}")

    for otb_path in otb_paths:
        mapping = read_otb_mapping(otb_path)
        if mapping.get(LIGHTNING_SERVER_ID) != LIGHTNING_CLIENT_ID:
            raise ValueError(f"Missing lightning mapping in {otb_path}")
        if mapping.get(INFERNAL_SERVER_ID) != INFERNAL_CLIENT_ID:
            raise ValueError(f"Missing infernal mapping in {otb_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Install lightning/infernal tier portals.")
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--verify-installed", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    assets_root = (args.assets or Path(__file__).resolve().parent).resolve()
    dat = repo / "sources/otclient-redemption/data/things/772/Tibia.dat"
    spr = repo / "sources/otclient-redemption/data/things/772/Tibia.spr"
    server_otb = repo / "server/data/items/items.otb"
    source_otb = repo / "sources/nekiro-tfs-1.5-7.72/data/items/items.otb"

    for name, want in ASSET_SHA256.items():
        got = sha256(assets_root / name)
        if got != want:
            raise ValueError(f"Unexpected SHA-256 for {name}: {got}; expected {want}")

    sprite_map = {
        "lightning_portal": [SPR_COUNT_CRYSTALS_ONLY + 1, SPR_COUNT_CRYSTALS_ONLY + 2, SPR_COUNT_CRYSTALS_ONLY + 3],
        "infernal_portal": [SPR_COUNT_CRYSTALS_ONLY + 4, SPR_COUNT_CRYSTALS_ONLY + 5, SPR_COUNT_CRYSTALS_ONLY + 6],
    }

    if args.verify_installed:
        verify_outputs(dat, spr, [server_otb, source_otb], sprite_map)
        print("verified: lightning 26402->5119 and infernal 26403->5120 installed; 1949 onTop")
        print(json.dumps({"dat": sha256(dat), "spr": sha256(spr), "otb": sha256(server_otb)}, indent=2))
        return

    verify_hashes(repo, PRE_UNINSTALL_SHA256)
    uninstall_old_portal(dat, spr, server_otb)

    work_dir = repo / "tools/assets/.tier_portals_work"
    work_dir.mkdir(parents=True, exist_ok=True)
    generated_spr = work_dir / "Tibia.spr"
    generated_dat = work_dir / "Tibia.dat"
    generated_otb = work_dir / "items.otb"

    built_sprites = build_spr(spr, generated_spr, assets_root)
    build_dat(dat, generated_dat, built_sprites)
    build_otb(server_otb, generated_otb)
    verify_outputs(generated_dat, generated_spr, [generated_otb], built_sprites)

    generated_dat.replace(dat)
    generated_spr.replace(spr)
    generated_otb.replace(server_otb)
    import shutil
    shutil.copy2(server_otb, source_otb)
    verify_outputs(dat, spr, [server_otb, source_otb], built_sprites)
    shutil.rmtree(work_dir)

    print(json.dumps({
        "lightning": {"serverId": LIGHTNING_SERVER_ID, "clientId": LIGHTNING_CLIENT_ID, "sprites": built_sprites["lightning_portal"]},
        "infernal": {"serverId": INFERNAL_SERVER_ID, "clientId": INFERNAL_CLIENT_ID, "sprites": built_sprites["infernal_portal"]},
        "datSha256": sha256(dat),
        "sprSha256": sha256(spr),
        "otbSha256": sha256(server_otb),
    }, indent=2))


if __name__ == "__main__":
    main()
