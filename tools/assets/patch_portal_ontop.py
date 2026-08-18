#!/usr/bin/env python3
"""Makes the elite summoning portal (server 1387 / client 1949) render on
top of creatures and corpses.

The 7.72 DAT record of client item 1949 carries attribute 2 (OnBottom),
which the client draws in the lowest tile layer - the portal disappeared
under the corpse. Swapping that single byte to attribute 3 (OnTop) moves
the portal to the client's drawTop phase, rendered after creatures. The
swap keeps the record size identical, so no other DAT offset changes.

The client ignores OnTop items when resolving tile clicks
(Tile::getTopMultiUseThing), so the corpse underneath stays lootable.
"""
import argparse
import hashlib
import shutil
from datetime import date
from pathlib import Path

from extract_thing_assets import Reader, adjust_attr_for_version, skip_attr_payload
from patch_water_dat_spr_exact import scan_dat_records

DAT_RELATIVE = Path("sources/otclient-redemption/data/things/772/Tibia.dat")
PORTAL_CLIENT_ID = 1949

ATTR_ONBOTTOM = 2
ATTR_ON_TOP = 3
ATTR_TERMINATOR = 0xFF

# DAT state after install_tier_portals.py (lightning/infernal portals present,
# 1949 still vanilla OnBottom).
EXPECTED_INPUT_SHA256 = "47EBFB23D7D92BE2954660E96F3C289DB80AB257FD76DA9AAD6A8EE59EA3AF24"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Swap DAT attribute OnBottom -> OnTop for the elite portal item (client 1949)."
    )
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument(
        "--verify-installed",
        action="store_true",
        help="Read-only: confirm the active DAT already carries the OnTop attribute.",
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    dat_path = repo / DAT_RELATIVE
    if not dat_path.is_file():
        raise ValueError(f"DAT not found: {dat_path}")

    data = bytearray(dat_path.read_bytes())
    header_end, records = scan_dat_records(bytes(data), 772)
    record = next(
        (r for r in records if r.category == 0 and r.client_id == PORTAL_CLIENT_ID),
        None,
    )
    if record is None:
        raise ValueError(f"Client item {PORTAL_CLIENT_ID} not found in DAT")

    # Walk the attribute list honoring payloads so payload bytes are never
    # mistaken for attribute ids.
    reader = Reader(bytes(data))
    reader.offset = record.start
    attribute_offsets: list[tuple[int, int]] = []
    for _ in range(255):
        attr_offset = reader.offset
        raw_attr = reader.get_u8()
        if raw_attr == ATTR_TERMINATOR:
            break
        attr = adjust_attr_for_version(raw_attr, 772)
        attribute_offsets.append((attr_offset, attr))
        skip_attr_payload(reader, attr, 772)
    else:
        raise ValueError("Portal record attributes are not terminated")
    if reader.offset != record.geometry_start:
        raise ValueError("Attribute walk did not end at the geometry section")

    has_on_top = any(attr == ATTR_ON_TOP for _, attr in attribute_offsets)
    onbottom_positions = [
        offset for offset, attr in attribute_offsets if attr == ATTR_ONBOTTOM
    ]

    if args.verify_installed:
        if not has_on_top or onbottom_positions:
            raise ValueError(
                f"DAT not patched as expected: attrs={[attr for _, attr in attribute_offsets]} "
                f"(onTop={has_on_top}, onBottomOffsets={onbottom_positions})"
            )
        print(f"verified: client item {PORTAL_CLIENT_ID} carries OnTop; sha256={sha256(dat_path)}")
        return

    current_sha = sha256(dat_path)
    if current_sha != EXPECTED_INPUT_SHA256:
        raise ValueError(
            f"Unexpected DAT SHA-256 {current_sha}; expected {EXPECTED_INPUT_SHA256}"
        )
    if has_on_top:
        raise ValueError("Portal record already carries the OnTop attribute")
    if len(onbottom_positions) != 1:
        raise ValueError(
            f"Expected exactly one OnBottom attribute, found {len(onbottom_positions)}"
        )

    backup_dir = repo / "backup-extras" / f"portal-ontop-{PORTAL_CLIENT_ID}-{date.today():%Y-%m-%d}"
    backup_dir.mkdir(parents=True, exist_ok=False)
    backup_path = backup_dir / "Tibia.dat"
    shutil.copy2(dat_path, backup_path)

    # Single-byte swap: same record size, every other DAT offset stays valid.
    data[onbottom_positions[0]] = ATTR_ON_TOP
    dat_path.write_bytes(bytes(data))

    _, patched_records = scan_dat_records(bytes(data), 772)
    patched = next(
        r for r in patched_records if r.category == 0 and r.client_id == PORTAL_CLIENT_ID
    )
    patched_reader = Reader(bytes(data))
    patched_reader.offset = patched.start
    patched_attrs: list[int] = []
    for _ in range(255):
        raw_attr = patched_reader.get_u8()
        if raw_attr == ATTR_TERMINATOR:
            break
        attr = adjust_attr_for_version(raw_attr, 772)
        patched_attrs.append(attr)
        skip_attr_payload(patched_reader, attr, 772)
    else:
        raise ValueError("Patched record attributes are not terminated")
    if ATTR_ON_TOP not in patched_attrs or ATTR_ONBOTTOM in patched_attrs:
        raise ValueError(f"Patched attributes are wrong: {patched_attrs}")
    if patched_reader.offset != patched.geometry_start:
        raise ValueError("Patched attribute walk did not end at the geometry section")
    if patched.end - patched.start != record.end - record.start:
        raise ValueError("Patched record changed size")

    new_sha = sha256(dat_path)
    print(f"backup: {backup_path}")
    print(f"patched: client item {PORTAL_CLIENT_ID} OnBottom -> OnTop")
    print(f"new sha256: {new_sha}")


if __name__ == "__main__":
    main()
