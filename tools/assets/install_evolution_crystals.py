#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import struct
from dataclasses import dataclass
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
FLAG_USEABLE = 1 << 4
FLAG_PICKUPABLE = 1 << 5
FLAG_MOVEABLE = 1 << 6

# DAT flag prefix of the gold converter (client 5095): 0x07 (pickupable-class)
# + 0x10 (useable, enables use/use-with targeting) + 0xFF terminator. Same
# interaction class as the evolution crystals.
EXPECTED_DAT_TEMPLATE_PREFIX = bytes.fromhex("0710ff")
DAT_TEMPLATE_CLIENT_ID = 5095


@dataclass(frozen=True)
class EvolutionCrystal:
    server_id: int
    client_id: int
    name: str
    source_name: str
    frames: int


ITEMS = (
    EvolutionCrystal(26399, 5116, "Spark", "Spark_crystal.gif", 8),
    EvolutionCrystal(26400, 5117, "Lightning", "Lightning_Crystal.gif", 8),
    EvolutionCrystal(26401, 5118, "Infernal", "Infernal_Crystal.gif", 8),
)

BASE_SHA256 = {
    "sources/otclient-redemption/data/things/772/Tibia.dat": "EFA023F91B2090DAEAE34BC6F78F735B03020F1AD75F315A33F3C8F533309724",
    "sources/otclient-redemption/data/things/772/Tibia.spr": "F6C8E1C2FF1B714C04CAE9D41D643B5BEFAB5D86F837AC49976E85D0693B1A02",
    "server/data/items/items.otb": "530488D36E11D174146906449E5DAB7224EFE52D6119CAF1920B5929E46EBDF8",
    "sources/nekiro-tfs-1.5-7.72/data/items/items.otb": "530488D36E11D174146906449E5DAB7224EFE52D6119CAF1920B5929E46EBDF8",
}

INSTALLED_SHA256 = {
    # State after install_tier_portals.py + patch_portal_ontop.py (2026-08-17):
    # lightning portal 26402 -> 5119, infernal portal 26403 -> 5120 and
    # 1949 with OnTop (tier 1 portal renders above corpses/creatures).
    "sources/otclient-redemption/data/things/772/Tibia.dat": "25E4EB612BA194039A245D3DB2905C86D215B4126C3FEF433FCF81D58BB95F72",
    "sources/otclient-redemption/data/things/772/Tibia.spr": "EA6D6C66AB506FBB9903C6BD9D09BF238D90F43EF390FE0B257F379B2F144667",
    "server/data/items/items.otb": "3B7FB1EC9B19DB347F631A9BFF8B9DAAB111353F504AB99F694AE2888BE53592",
    "sources/nekiro-tfs-1.5-7.72/data/items/items.otb": "3B7FB1EC9B19DB347F631A9BFF8B9DAAB111353F504AB99F694AE2888BE53592",
}

ASSET_SHA256 = {
    "Spark_crystal.gif": "8C9BBB253E5724BBB76102008D33368E6E0EF451EEA0511FEAF72ED026A2432B",
    "Lightning_Crystal.gif": "7CD0F62521F991C28491D64EB916425F8F29B23427CA199653A27F36781FAD9F",
    "Infernal_Crystal.gif": "D88846E5973244DACB017BB14FBF1303B7B8925508ACD214A764639C1E1A427C",
}

BASE_SPR_COUNT = 16195
# +24 crystal frames; the active SPR also carries the 6 tier portal frames
# appended later by install_tier_portals.py (16195 + 24 + 6 = 16225).
INSTALLED_SPR_COUNT = 16225
BASE_DAT_LAST_ITEM = 5115


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_hashes(repo: Path, expected_hashes: dict[str, str]) -> dict[str, str]:
    actual_hashes: dict[str, str] = {}
    for relative_path, expected_hash in expected_hashes.items():
        path = repo / relative_path
        if not path.is_file():
            raise ValueError(f"Required binary is missing: {path}")
        actual_hash = sha256(path).upper()
        if actual_hash != expected_hash:
            raise ValueError(
                f"Unexpected SHA-256 for {path}: {actual_hash}; expected {expected_hash}"
            )
        actual_hashes[relative_path] = actual_hash
    return actual_hashes


def verify_assets(assets_root: Path) -> None:
    for relative_path, expected_hash in ASSET_SHA256.items():
        path = assets_root / relative_path
        if not path.is_file():
            raise ValueError(f"Evolution crystal source asset is missing: {path}")
        actual_hash = sha256(path).upper()
        if actual_hash != expected_hash:
            raise ValueError(
                f"Unexpected SHA-256 for {path}: {actual_hash}; expected {expected_hash}"
            )


def load_frames(source: Path, expected_frames: int) -> list[Image.Image]:
    with Image.open(source) as image:
        frame_count = getattr(image, "n_frames", 1)
        if frame_count != expected_frames:
            raise ValueError(
                f"{source} has {frame_count} frame(s); expected {expected_frames}"
            )

        frames: list[Image.Image] = []
        for frame_index in range(frame_count):
            image.seek(frame_index)
            frame = image.convert("RGBA")
            if frame.size != (32, 32):
                raise ValueError(
                    f"{source} frame {frame_index} is {frame.width}x{frame.height}; expected 32x32"
                )
            frames.append(frame.copy())
        return frames


def build_spr(input_spr: Path, output_spr: Path, assets_root: Path) -> tuple[int, dict[int, list[int]]]:
    signature, old_count, sprites = read_spr(input_spr)
    if old_count != BASE_SPR_COUNT:
        raise ValueError(f"Expected the active SPR to end at {BASE_SPR_COUNT}, found {old_count}")

    next_sprite_id = old_count + 1
    item_sprites: dict[int, list[int]] = {}
    for item in ITEMS:
        source = assets_root / item.source_name
        frames = load_frames(source, item.frames)
        sprite_ids: list[int] = []
        for frame in frames:
            sprites[next_sprite_id] = encode_sprite(frame)
            sprite_ids.append(next_sprite_id)
            next_sprite_id += 1
        item_sprites[item.client_id] = sprite_ids

    new_count = next_sprite_id - 1
    write_spr(output_spr, signature, new_count, sprites)
    return new_count, item_sprites


def build_dat(
    input_dat: Path,
    output_dat: Path,
    item_sprites: dict[int, list[int]],
) -> None:
    data = input_dat.read_bytes()
    item_last_id, outfit_last_id, effect_last_id, missile_last_id = struct.unpack_from("<4H", data, 4)
    if item_last_id != BASE_DAT_LAST_ITEM:
        raise ValueError(
            f"Expected the active DAT to end at client item {BASE_DAT_LAST_ITEM}, found {item_last_id}"
        )

    _, records = scan_dat_records(data, 772)
    records_by_client_id = {
        record.client_id: record for record in records if record.category == THING_CATEGORY_ITEM
    }
    first_outfit = next(record for record in records if record.category == 1)
    template = records_by_client_id[DAT_TEMPLATE_CLIENT_ID]
    template_prefix = data[template.start:template.geometry_start]
    if template_prefix != EXPECTED_DAT_TEMPLATE_PREFIX:
        raise ValueError(
            f"DAT template {DAT_TEMPLATE_CLIENT_ID} prefix changed: {template_prefix.hex()}"
        )

    appended = bytearray()
    for item in ITEMS:
        appended.extend(template_prefix)
        sprite_ids = item_sprites[item.client_id]
        appended.extend(bytes((1, 1, 1, 1, 1, 1, len(sprite_ids))))
        for sprite_id in sprite_ids:
            appended.extend(struct.pack("<H", sprite_id))

    output = bytearray(data[:first_outfit.start])
    output.extend(appended)
    output.extend(data[first_outfit.start:])
    struct.pack_into(
        "<4H",
        output,
        4,
        ITEMS[-1].client_id,
        outfit_last_id,
        effect_last_id,
        missile_last_id,
    )
    output_dat.write_bytes(output)


def escape_otb_properties(properties: bytes) -> bytes:
    output = bytearray()
    for value in properties:
        if value in (OTB_NODE_ESCAPE, OTB_NODE_START, OTB_NODE_END):
            output.append(OTB_NODE_ESCAPE)
        output.append(value)
    return bytes(output)


def build_otb(input_otb: Path, output_otb: Path) -> None:
    mapping = read_otb_mapping(input_otb)
    if max(mapping) != 26398 or max(mapping.values()) != 5115:
        raise ValueError(
            "Expected the active OTB mapping to end at server 26398 / client 5115"
        )
    for item in ITEMS:
        if item.server_id in mapping or item.client_id in mapping.values():
            raise ValueError(
                f"Requested OTB mapping is already occupied: {item.server_id}->{item.client_id}"
            )

    data = input_otb.read_bytes()
    if not data or data[-1] != OTB_NODE_END:
        raise ValueError(f"{input_otb} does not end with the root OTB node terminator")

    nodes = bytearray()
    for item in ITEMS:
        flags = FLAG_USEABLE | FLAG_PICKUPABLE | FLAG_MOVEABLE
        properties = struct.pack(
            "<IBHHBHH",
            flags,
            ITEM_ATTR_SERVERID,
            2,
            item.server_id,
            ITEM_ATTR_CLIENTID,
            2,
            item.client_id,
        )
        nodes.append(OTB_NODE_START)
        nodes.append(0)
        nodes.extend(escape_otb_properties(properties))
        nodes.append(OTB_NODE_END)

    output_otb.write_bytes(data[:-1] + nodes + bytes((OTB_NODE_END,)))


def backup_files(repo: Path, backup_dir: Path, relative_paths: list[Path]) -> dict[str, str]:
    backup_hashes: dict[str, str] = {}
    for relative_path in relative_paths:
        source = repo / relative_path
        target = backup_dir / relative_path
        if target.exists():
            raise ValueError(f"Backup target already exists: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        backup_hashes[str(relative_path)] = sha256(target)
    return backup_hashes


def verify_outputs(dat: Path, spr: Path, otb_paths: list[Path], item_sprites: dict[int, list[int]]) -> None:
    _, sprite_count, sprites = read_spr(spr)
    if sprite_count != INSTALLED_SPR_COUNT:
        raise ValueError(
            f"Expected {INSTALLED_SPR_COUNT} sprites after installation, found {sprite_count}"
        )

    client_ids = {item.client_id for item in ITEMS}
    things = parse_dat(dat, 772, THING_CATEGORY_ITEM, client_ids)
    for item in ITEMS:
        thing = things[item.client_id]
        if (
            thing.width,
            thing.height,
            thing.layers,
            thing.pattern_x,
            thing.pattern_y,
            thing.pattern_z,
            thing.frames,
        ) != (1, 1, 1, 1, 1, 1, item.frames):
            raise ValueError(f"Unexpected DAT layout for client item {item.client_id}")
        if thing.sprites != item_sprites[item.client_id]:
            raise ValueError(f"Unexpected DAT sprite order for client item {item.client_id}")
        if any(sprite_id not in sprites for sprite_id in thing.sprites):
            raise ValueError(f"Missing SPR payload for client item {item.client_id}")

    expected_mapping = {item.server_id: item.client_id for item in ITEMS}
    for otb_path in otb_paths:
        mapping = read_otb_mapping(otb_path)
        actual = {server_id: mapping.get(server_id) for server_id in expected_mapping}
        if actual != expected_mapping:
            raise ValueError(f"Unexpected OTB mapping in {otb_path}: {actual}")


def expected_item_sprites() -> dict[int, list[int]]:
    next_sprite_id = BASE_SPR_COUNT + 1
    result: dict[int, list[int]] = {}
    for item in ITEMS:
        result[item.client_id] = list(range(next_sprite_id, next_sprite_id + item.frames))
        next_sprite_id += item.frames
    return result


def verify_sprite_payloads(
    spr: Path, assets_root: Path, item_sprites: dict[int, list[int]]
) -> None:
    _, _, sprites = read_spr(spr)
    for item in ITEMS:
        source = assets_root / item.source_name
        frames = load_frames(source, item.frames)
        for frame, sprite_id in zip(frames, item_sprites[item.client_id]):
            if sprites.get(sprite_id) != encode_sprite(frame):
                raise ValueError(
                    f"SPR payload {sprite_id} does not match {source}"
                )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Install the 3 Evolution Crystals into the active 7.72 DAT, SPR and TFS OTB files."
    )
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument(
        "--assets",
        type=Path,
        help="Source GIF directory (defaults to tools/assets/evolution_crystals).",
    )
    parser.add_argument("--backup-dir", type=Path)
    parser.add_argument(
        "--verify-installed",
        action="store_true",
        help="Read-only verification of the active installed DAT/SPR/OTB files.",
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    assets_root = (
        args.assets.resolve()
        if args.assets
        else (Path(__file__).resolve().parent / "evolution_crystals")
    )
    dat_relative = Path("sources/otclient-redemption/data/things/772/Tibia.dat")
    spr_relative = Path("sources/otclient-redemption/data/things/772/Tibia.spr")
    server_otb_relative = Path("server/data/items/items.otb")
    source_otb_relative = Path("sources/nekiro-tfs-1.5-7.72/data/items/items.otb")
    binary_paths = [dat_relative, spr_relative, server_otb_relative, source_otb_relative]

    verify_assets(assets_root)

    if args.verify_installed:
        item_sprites = expected_item_sprites()
        if INSTALLED_SHA256:
            verify_hashes(repo, INSTALLED_SHA256)
        verify_outputs(
            repo / dat_relative,
            repo / spr_relative,
            [repo / server_otb_relative, repo / source_otb_relative],
            item_sprites,
        )
        verify_sprite_payloads(repo / spr_relative, assets_root, item_sprites)
        print(
            json.dumps(
                {
                    "verified": True,
                    "assetsRoot": str(assets_root),
                    "activeSha256": {
                        str(dat_relative): sha256(repo / dat_relative),
                        str(spr_relative): sha256(repo / spr_relative),
                        str(server_otb_relative): sha256(repo / server_otb_relative),
                        str(source_otb_relative): sha256(repo / source_otb_relative),
                    },
                },
                indent=2,
            )
        )
        return

    if args.backup_dir is None:
        parser.error("--backup-dir is required unless --verify-installed is used")

    backup_dir = args.backup_dir.resolve()
    verify_hashes(repo, BASE_SHA256)

    backup_hashes = backup_files(repo, backup_dir, binary_paths)

    dat = repo / dat_relative
    spr = repo / spr_relative
    server_otb = repo / server_otb_relative
    source_otb = repo / source_otb_relative
    work_dir = backup_dir / "generated"
    work_dir.mkdir(parents=True, exist_ok=True)
    generated_dat = work_dir / "Tibia.dat"
    generated_spr = work_dir / "Tibia.spr"
    generated_otb = work_dir / "items.otb"

    sprite_count, item_sprites = build_spr(spr, generated_spr, assets_root)
    build_dat(dat, generated_dat, item_sprites)
    build_otb(server_otb, generated_otb)
    verify_outputs(generated_dat, generated_spr, [generated_otb], item_sprites)

    shutil.copy2(generated_dat, dat)
    shutil.copy2(generated_spr, spr)
    shutil.copy2(generated_otb, server_otb)
    shutil.copy2(generated_otb, source_otb)
    verify_outputs(dat, spr, [server_otb, source_otb], item_sprites)
    verify_sprite_payloads(spr, assets_root, item_sprites)

    shutil.rmtree(work_dir)

    summary = {
        "backupDir": str(backup_dir),
        "backupSha256": backup_hashes,
        "serverIds": [item.server_id for item in ITEMS],
        "clientIds": [item.client_id for item in ITEMS],
        "spriteCount": sprite_count,
        "spriteIdsByClientId": {
            str(client_id): sprite_ids for client_id, sprite_ids in item_sprites.items()
        },
        "activeSha256": {
            str(dat_relative): sha256(dat),
            str(spr_relative): sha256(spr),
            str(server_otb_relative): sha256(server_otb),
            str(source_otb_relative): sha256(source_otb),
        },
    }
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
