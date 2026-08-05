#!/usr/bin/env python3
import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from extract_sprites import decode_sprite, save_sprite_sheet_clean


THING_ATTR_DISPLACEMENT = 24
THING_ATTR_LIGHT = 21
THING_ATTR_ELEVATION = 25
THING_ATTR_GROUND = 0
THING_ATTR_WRITABLE = 8
THING_ATTR_WRITABLE_ONCE = 9
THING_ATTR_MINIMAP_COLOR = 28
THING_ATTR_CLOTH = 32
THING_ATTR_LENS_HELP = 29
THING_ATTR_DEFAULT_ACTION = 251
THING_ATTR_FLOOR_CHANGE = 252
THING_ATTR_CHARGEABLE = 254
THING_LAST_ATTR = 255

THING_CATEGORY_ITEM = 0
THING_CATEGORY_CREATURE = 1
THING_CATEGORY_EFFECT = 2
THING_CATEGORY_MISSILE = 3


@dataclass
class ThingInfo:
    client_id: int
    category: int
    attrs: list[int]
    width: int
    height: int
    layers: int
    pattern_x: int
    pattern_y: int
    pattern_z: int
    frames: int
    sprites: list[int]

    @property
    def unique_sprites(self) -> list[int]:
        result: list[int] = []
        seen: set[int] = set()
        for sprite_id in self.sprites:
            if sprite_id <= 0 or sprite_id in seen:
                continue
            result.append(sprite_id)
            seen.add(sprite_id)
        return result


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def get_u8(self) -> int:
        value = self.data[self.offset]
        self.offset += 1
        return value

    def get_u16(self) -> int:
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def get_u32(self) -> int:
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value


def category_name(category: int) -> str:
    if category == THING_CATEGORY_ITEM:
        return "item"
    if category == THING_CATEGORY_CREATURE:
        return "creature"
    if category == THING_CATEGORY_EFFECT:
        return "effect"
    if category == THING_CATEGORY_MISSILE:
        return "missile"
    return f"category-{category}"


def adjust_attr_for_version(attr: int, version: int) -> int:
    if version >= 1000:
        if attr == 16:
            return 253
        if attr == 254:
            return 34
        if attr == 35:
            return 251
        if attr > 16:
            return attr - 1
        return attr
    if version >= 860:
        return attr
    if version >= 780:
        if attr == 8:
            return THING_ATTR_CHARGEABLE
        if attr > 8:
            return attr - 1
        return attr
    if version >= 755:
        if attr == 23:
            return THING_ATTR_FLOOR_CHANGE
        return attr
    if version >= 740:
        if 0 < attr <= 15:
            attr += 1
        elif attr == 16:
            attr = THING_ATTR_LIGHT
        elif attr == 17:
            attr = THING_ATTR_FLOOR_CHANGE
        elif attr == 18:
            attr = 30
        elif attr == 19:
            attr = THING_ATTR_ELEVATION
        elif attr == 20:
            attr = THING_ATTR_DISPLACEMENT
        elif attr == 22:
            attr = THING_ATTR_MINIMAP_COLOR
        elif attr == 23:
            attr = 20
        elif attr == 24:
            attr = 26
        elif attr == 25:
            attr = 17
        elif attr == 26:
            attr = 18
        elif attr == 27:
            attr = 19
        elif attr == 28:
            attr = 27

        if attr == 7:
            return 6
        if attr == 6:
            return 7
        return attr
    return attr


def skip_attr_payload(reader: Reader, attr: int, version: int) -> None:
    if attr == THING_ATTR_CHARGEABLE:
        return
    if attr == THING_ATTR_DISPLACEMENT:
        if version >= 755:
            reader.get_u16()
            reader.get_u16()
        return
    if attr == THING_ATTR_LIGHT:
        reader.get_u16()
        reader.get_u16()
        return
    if attr in (
        THING_ATTR_ELEVATION,
        THING_ATTR_GROUND,
        THING_ATTR_WRITABLE,
        THING_ATTR_WRITABLE_ONCE,
        THING_ATTR_MINIMAP_COLOR,
        THING_ATTR_CLOTH,
        THING_ATTR_LENS_HELP,
        THING_ATTR_DEFAULT_ACTION,
    ):
        reader.get_u16()


def parse_thing(reader: Reader, version: int, category: int, client_id: int) -> ThingInfo:
    attrs: list[int] = []
    for _ in range(THING_LAST_ATTR):
        raw_attr = reader.get_u8()
        if raw_attr == THING_LAST_ATTR:
            break
        attr = adjust_attr_for_version(raw_attr, version)
        attrs.append(attr)
        skip_attr_payload(reader, attr, version)
    else:
        raise RuntimeError(f"Thing {client_id} did not terminate attrs correctly")

    has_frame_groups = category == THING_CATEGORY_CREATURE and version >= 1050
    group_count = reader.get_u8() if has_frame_groups else 1

    width = 1
    height = 1
    layers = 1
    pattern_x = 1
    pattern_y = 1
    pattern_z = 1
    total_frames = 0
    sprites: list[int] = []

    for _group in range(group_count):
        if has_frame_groups:
            reader.get_u8()  # frame group type

        width = reader.get_u8()
        height = reader.get_u8()
        if width > 1 or height > 1:
            reader.get_u8()  # real size

        layers = reader.get_u8()
        pattern_x = reader.get_u8()
        pattern_y = reader.get_u8()
        pattern_z = reader.get_u8() if version >= 755 else 1
        frames = reader.get_u8()
        total_frames += frames

        total_sprites = width * height * layers * pattern_x * pattern_y * pattern_z * frames
        for _ in range(total_sprites):
            sprite_id = reader.get_u32() if version >= 960 else reader.get_u16()
            sprites.append(sprite_id)

    return ThingInfo(
        client_id=client_id,
        category=category,
        attrs=attrs,
        width=width,
        height=height,
        layers=layers,
        pattern_x=pattern_x,
        pattern_y=pattern_y,
        pattern_z=pattern_z,
        frames=total_frames,
        sprites=sprites,
    )


def parse_dat(dat_path: Path, version: int, category: int, target_ids: set[int] | None) -> dict[int, ThingInfo]:
    data = dat_path.read_bytes()
    reader = Reader(data)

    reader.get_u32()  # signature
    category_counts = [reader.get_u16() + 1 for _ in range(4)]

    results: dict[int, ThingInfo] = {}
    for current_category in range(4):
        count = category_counts[current_category]
        first_id = 100 if current_category == THING_CATEGORY_ITEM else 1
        for client_id in range(first_id, count):
            thing = parse_thing(reader, version, current_category, client_id)
            if current_category == category and (target_ids is None or client_id in target_ids):
                results[client_id] = thing
    return results


def write_metadata(out_dir: Path, thing: ThingInfo) -> None:
    payload = {
        "clientId": thing.client_id,
        "category": category_name(thing.category),
        "attrs": thing.attrs,
        "isGround": THING_ATTR_GROUND in thing.attrs,
        "size": {"width": thing.width, "height": thing.height},
        "layers": thing.layers,
        "patterns": {"x": thing.pattern_x, "y": thing.pattern_y, "z": thing.pattern_z},
        "frames": thing.frames,
        "sprites": thing.sprites,
        "uniqueSprites": thing.unique_sprites,
    }
    (out_dir / "metadata.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")


def maybe_build_pattern_mosaic(out_dir: Path, thing: ThingInfo) -> None:
    if THING_ATTR_GROUND not in thing.attrs:
        return
    if thing.width != 1 or thing.height != 1 or thing.layers != 1 or thing.frames != 1:
        return

    expected = thing.pattern_x * thing.pattern_y * thing.pattern_z
    sprite_ids = thing.unique_sprites
    if thing.pattern_z != 1 or len(sprite_ids) != expected:
        return

    from tile_mosaic import build_mosaic  # local import to avoid startup dependency

    class Args:
        tiles = str(out_dir)
        ids = ",".join(str(x) for x in sprite_ids)
        pattern_width = thing.pattern_x
        pattern_height = thing.pattern_y
        repeat_x = max(8, thing.pattern_x * 2)
        repeat_y = max(8, thing.pattern_y * 2)
        out = str(out_dir / "mosaic-input.png")

    build_mosaic(Args())


def extract_assets(dat_path: Path, spr_path: Path, version: int, category: int, ids: list[int], out_root: Path) -> None:
    things = parse_dat(dat_path, version, category, set(ids))
    missing = [thing_id for thing_id in ids if thing_id not in things]
    if missing:
        raise SystemExit(f"Client ids not found in DAT: {', '.join(str(x) for x in missing)}")

    index: list[dict[str, object]] = []
    for thing_id in ids:
        thing = things[thing_id]
        out_dir = out_root / f"{category_name(category)}-{thing_id}"
        out_dir.mkdir(parents=True, exist_ok=True)
        write_metadata(out_dir, thing)

        extracted: list[tuple[int, object]] = []
        for sprite_id in thing.unique_sprites:
            image = decode_sprite(spr_path, sprite_id, 32, False, False)
            if image is None:
                continue
            image.save(out_dir / f"{sprite_id}.png")
            extracted.append((sprite_id, image))

        save_sprite_sheet_clean(extracted, out_dir / "sheet.png", columns=min(12, max(1, len(extracted))), tile_size=32)
        maybe_build_pattern_mosaic(out_dir, thing)

        index.append(
            {
                "clientId": thing_id,
                "folder": str(out_dir),
                "sheet": str(out_dir / "sheet.png"),
                "mosaic": str(out_dir / "mosaic-input.png") if (out_dir / "mosaic-input.png").exists() else None,
                "pngCount": len(extracted),
            }
        )
        print(f"[ok] prepared {out_dir}")

    (out_root / "index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")
    print(f"[ok] wrote {out_root / 'index.json'}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract per-client-id assets from Tibia.dat and Tibia.spr.")
    parser.add_argument("--dat", required=True, help="Path to Tibia.dat")
    parser.add_argument("--spr", required=True, help="Path to Tibia.spr")
    parser.add_argument("--ids", required=True, help="Comma-separated client ids")
    parser.add_argument("--category", default="item", choices=["item", "creature", "effect", "missile"])
    parser.add_argument("--version", type=int, default=772, help="Client version, default: 772")
    parser.add_argument("--out-root", required=True, help="Directory where per-asset folders will be written")
    args = parser.parse_args()

    category_map = {
        "item": THING_CATEGORY_ITEM,
        "creature": THING_CATEGORY_CREATURE,
        "effect": THING_CATEGORY_EFFECT,
        "missile": THING_CATEGORY_MISSILE,
    }
    ids = [int(chunk.strip()) for chunk in args.ids.split(",") if chunk.strip()]
    extract_assets(
        Path(args.dat),
        Path(args.spr),
        args.version,
        category_map[args.category],
        ids,
        Path(args.out_root),
    )


if __name__ == "__main__":
    main()
