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


@dataclass(frozen=True)
class TrainingItem:
    server_id: int
    client_id: int
    tier: str
    kind: str
    source_name: str
    frames: int


ITEMS = (
    TrainingItem(26379, 5096, "Common", "sword", "Training_Sword.gif", 1),
    TrainingItem(26380, 5097, "Common", "axe", "Training_Axe.gif", 1),
    TrainingItem(26381, 5098, "Common", "club", "Training_Club.gif", 1),
    TrainingItem(26382, 5099, "Common", "spear", "Training_spear.gif", 1),
    TrainingItem(26383, 5100, "Common", "shield", "Training_Shield.gif", 1),
    TrainingItem(26384, 5101, "Spark", "sword", "Spark_training_sword.gif", 5),
    TrainingItem(26385, 5102, "Spark", "axe", "Spark_training_axe.gif", 5),
    TrainingItem(26386, 5103, "Spark", "club", "Spark_training_club.gif", 5),
    TrainingItem(26387, 5104, "Spark", "spear", "Spark_training_spear.gif", 5),
    TrainingItem(26388, 5105, "Spark", "shield", "Spark_training_shield.gif", 5),
    TrainingItem(26389, 5106, "Light", "sword", "Lightning_training_sword.gif", 5),
    TrainingItem(26390, 5107, "Light", "axe", "Lightning_training_axe.gif", 5),
    TrainingItem(26391, 5108, "Light", "club", "Lightning_training_club.gif", 5),
    TrainingItem(26392, 5109, "Light", "spear", "Lightning_training_spear.gif", 5),
    TrainingItem(26393, 5110, "Light", "shield", "Lightning_training_shield.gif", 5),
    TrainingItem(26394, 5111, "Inferno", "sword", "Infernal_training_sword.gif", 5),
    TrainingItem(26395, 5112, "Inferno", "axe", "Infernal_training_axe.gif", 5),
    TrainingItem(26396, 5113, "Inferno", "club", "Infernal_training_club.gif", 5),
    TrainingItem(26397, 5114, "Inferno", "spear", "Infernal_training_spear.gif", 5),
    TrainingItem(26398, 5115, "Inferno", "shield", "Infernal_training_shield.gif", 5),
)

BASE_SHA256 = {
    "sources/otclient-redemption/data/things/772/Tibia.dat": "0940A016A2BD8D60642016C661302B2E856898578D9DF4F340572A55AA3B93C0",
    "sources/otclient-redemption/data/things/772/Tibia.spr": "0F196EC677B56C2A08C7530662B49429A92CDC8DCBE769B5C399E376E25A90CD",
    "server/data/items/items.otb": "B8CB945B161927DDC8DA4994426E37857B5CB4C569FEA203E546512B56C8A2B6",
    "sources/nekiro-tfs-1.5-7.72/data/items/items.otb": "B8CB945B161927DDC8DA4994426E37857B5CB4C569FEA203E546512B56C8A2B6",
}

INSTALLED_SHA256 = {
    "sources/otclient-redemption/data/things/772/Tibia.dat": "EFA023F91B2090DAEAE34BC6F78F735B03020F1AD75F315A33F3C8F533309724",
    "sources/otclient-redemption/data/things/772/Tibia.spr": "F6C8E1C2FF1B714C04CAE9D41D643B5BEFAB5D86F837AC49976E85D0693B1A02",
    "server/data/items/items.otb": "530488D36E11D174146906449E5DAB7224EFE52D6119CAF1920B5929E46EBDF8",
    "sources/nekiro-tfs-1.5-7.72/data/items/items.otb": "530488D36E11D174146906449E5DAB7224EFE52D6119CAF1920B5929E46EBDF8",
}

ASSET_SHA256 = {
    "Common/Training_Sword.gif": "F7C03C282DB4F7FFA6ADA750F4DFB68BD958A24B1D0D7B505A8D811B5404C532",
    "Common/Training_Axe.gif": "1EECDC44B491CDFDD8058F9FF148C561DCE34AD863225DE9691CF33B23940A7B",
    "Common/Training_Club.gif": "5C901A15013A8AA8C6E2DD054E57F29FD560A23307BFC183C1149051AFDA4395",
    "Common/Training_spear.gif": "21D16E8274E156A0D0C5BA31C46FEB48FED54D306994FF72E5290FE6250CE086",
    "Common/Training_Shield.gif": "589B398D20C836756AF0D8147F0749DEFEF670A51A44E3F3C8768040C9101912",
    "Spark/Spark_training_sword.gif": "84377831137BDE897831C506A8CAD22E8814DA597A0B35684FB1ACAC4BA96ECB",
    "Spark/Spark_training_axe.gif": "CFEC6C911570B47564CAC4281A2E4C95A9557EB5608AD9B10E0481ED9DF24F46",
    "Spark/Spark_training_club.gif": "F1DF09877803E948769E858456B2A3C0AED6868A30E70CE15D16EA9CD129EA91",
    "Spark/Spark_training_spear.gif": "080244D3A3EC5FDA4DEBAFE91F466A5BD2D0320DD26EEED93814225235B2DFD2",
    "Spark/Spark_training_shield.gif": "89F3FB3984D7DFCE00A23DCF4987EB616045B4012F32A264B2A346E000BCF952",
    "Light/Lightning_training_sword.gif": "ACE5E316E27246DC9BBF0CC89C5EC0A5F52AA2D6BE1501FF3604828D58703B50",
    "Light/Lightning_training_axe.gif": "CA30B264D7F3EFB2F869BB82116F2216D22D43E9750F970466E94363E53D0C3F",
    "Light/Lightning_training_club.gif": "C77778CEA134C62E1559317354DFDF26B6B37D46007A3CB325A29943942CD78F",
    "Light/Lightning_training_spear.gif": "B6E6DD5B4AE2BE334DDAAC15C3CC564BD4C0DC71C2A8461647B5F160495390D0",
    "Light/Lightning_training_shield.gif": "7A7645FE5346ACB2D58475BA4A138F5A0EA65FA719F887A32846E58FD3D39F75",
    "Inferno/Infernal_training_sword.gif": "D80A3F415245CD97E77E359DE1EA3F5D28C89F8DA411358F3524B5467A4D29B2",
    "Inferno/Infernal_training_axe.gif": "EBA158F2A178E328A1531FE6D913E176F984AB60967C7BF8D28B7EB0F35DEA27",
    "Inferno/Infernal_training_club.gif": "29D53B01F35DF4D0E9F286AFC5D7BC92ECD853C02CC9D22A61912F4A5C493990",
    "Inferno/Infernal_training_spear.gif": "A505A05C1D2366EACA013C6AE04171E35BB4501A73A4301813E5B4C7274BD483",
    "Inferno/Infernal_training_shield.gif": "7825F1E9D65F24EF63FFE9DDA42F18A874BD55E477F26122993A3EF179318B06",
}


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
            raise ValueError(f"Training Item source asset is missing: {path}")
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
    if old_count != 16115:
        raise ValueError(f"Expected the active SPR to end at 16115, found {old_count}")

    next_sprite_id = old_count + 1
    item_sprites: dict[int, list[int]] = {}
    for item in ITEMS:
        source = assets_root / item.tier / item.source_name
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
    if item_last_id != 5095:
        raise ValueError(f"Expected the active DAT to end at client item 5095, found {item_last_id}")

    _, records = scan_dat_records(data, 772)
    item_records = [record for record in records if record.category == THING_CATEGORY_ITEM]
    first_outfit = next(record for record in records if record.category == 1)
    records_by_client_id = {record.client_id: record for record in item_records}
    weapon_template = records_by_client_id[3264]
    shield_template = records_by_client_id[3412]

    appended = bytearray()
    for item in ITEMS:
        template = shield_template if item.kind == "shield" else weapon_template
        appended.extend(data[template.start:template.geometry_start])
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
    if max(mapping) != 26378 or max(mapping.values()) != 5095:
        raise ValueError(
            "Expected the active OTB mapping to end at server 26378 / client 5095"
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
        flags = FLAG_PICKUPABLE | FLAG_MOVEABLE
        if item.kind != "shield":
            flags |= FLAG_USEABLE
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
    if sprite_count != 16195:
        raise ValueError(f"Expected 16195 sprites after installation, found {sprite_count}")

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
    next_sprite_id = 16116
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
        source = assets_root / item.tier / item.source_name
        frames = load_frames(source, item.frames)
        for frame, sprite_id in zip(frames, item_sprites[item.client_id]):
            if sprites.get(sprite_id) != encode_sprite(frame):
                raise ValueError(
                    f"SPR payload {sprite_id} does not match {source}"
                )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Install the 20 Training Items into the active 7.72 DAT, SPR and TFS OTB files."
    )
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument(
        "--assets",
        type=Path,
        help="Source GIF directory (defaults to tools/assets/training_items).",
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
        else (Path(__file__).resolve().parent / "training_items")
    )
    dat_relative = Path("sources/otclient-redemption/data/things/772/Tibia.dat")
    spr_relative = Path("sources/otclient-redemption/data/things/772/Tibia.spr")
    server_otb_relative = Path("server/data/items/items.otb")
    source_otb_relative = Path("sources/nekiro-tfs-1.5-7.72/data/items/items.otb")
    binary_paths = [dat_relative, spr_relative, server_otb_relative, source_otb_relative]

    verify_assets(assets_root)

    if args.verify_installed:
        item_sprites = expected_item_sprites()
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
                    "activeSha256": verify_hashes(repo, INSTALLED_SHA256),
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
    verify_hashes(repo, INSTALLED_SHA256)

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
