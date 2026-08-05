#!/usr/bin/env python3
import argparse
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw


def parse_ids(spec: str) -> list[int]:
    result: list[int] = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            start_s, end_s = chunk.split("-", 1)
            start = int(start_s)
            end = int(end_s)
            step = 1 if end >= start else -1
            result.extend(range(start, end + step, step))
        else:
            result.append(int(chunk))
    deduped: list[int] = []
    seen = set()
    for value in result:
        if value not in seen:
            deduped.append(value)
            seen.add(value)
    return deduped


def read_u16(data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<H", data, offset)[0], offset + 2


def read_u32(data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<I", data, offset)[0], offset + 4


def decode_sprite(
    spr_path: Path,
    sprite_id: int,
    sprite_size: int,
    use_u32_count: bool,
    alpha_channel: bool,
) -> Image.Image | None:
    raw = spr_path.read_bytes()
    offset = 4  # signature
    count_fmt = "<I" if use_u32_count else "<H"
    count_size = 4 if use_u32_count else 2
    sprites_count = struct.unpack_from(count_fmt, raw, offset)[0]
    offset += count_size

    if sprite_id <= 0 or sprite_id > sprites_count:
        return None

    addr_table = offset
    sprite_addr = struct.unpack_from("<I", raw, addr_table + (sprite_id - 1) * 4)[0]
    if sprite_addr == 0:
        return None

    cursor = sprite_addr
    cursor += 3  # color key
    pixel_data_size, cursor = read_u16(raw, cursor)
    sprite_payload = raw[cursor: cursor + pixel_data_size]

    img = Image.new("RGBA", (sprite_size, sprite_size), (0, 0, 0, 0))
    pixels = img.load()

    payload_offset = 0
    pixel_index = 0
    total_pixels = sprite_size * sprite_size
    channels = 4 if alpha_channel else 3

    while payload_offset + 4 <= len(sprite_payload) and pixel_index < total_pixels:
        transparent_pixels, payload_offset = read_u16(sprite_payload, payload_offset)
        colored_pixels, payload_offset = read_u16(sprite_payload, payload_offset)

        pixel_index += transparent_pixels

        for _ in range(colored_pixels):
            if payload_offset + channels > len(sprite_payload) or pixel_index >= total_pixels:
                break

            x = pixel_index % sprite_size
            y = pixel_index // sprite_size
            if alpha_channel:
                r, g, b, a = struct.unpack_from("<BBBB", sprite_payload, payload_offset)
            else:
                r, g, b = struct.unpack_from("<BBB", sprite_payload, payload_offset)
                a = 255
            pixels[x, y] = (r, g, b, a)
            payload_offset += channels
            pixel_index += 1

    return img


def save_sheet(images: list[tuple[int, Image.Image]], output: Path, columns: int, tile_size: int) -> None:
    if not images:
        return

    rows = math.ceil(len(images) / columns)
    label_height = 14
    sheet = Image.new("RGBA", (columns * tile_size, rows * (tile_size + label_height)), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)

    for index, (sprite_id, image) in enumerate(images):
        col = index % columns
        row = index // columns
        x = col * tile_size
        y = row * (tile_size + label_height)
        if image.size != (tile_size, tile_size):
            image = image.resize((tile_size, tile_size), Image.Resampling.NEAREST)
        sheet.alpha_composite(image, (x, y))
        draw.text((x + 1, y + tile_size), str(sprite_id), fill=(220, 220, 220, 255))

    sheet.save(output)


def save_sprite_sheet_clean(images: list[tuple[int, Image.Image]], output: Path, columns: int, tile_size: int) -> None:
    if not images:
        return

    rows = math.ceil(len(images) / columns)
    sheet = Image.new("RGBA", (columns * tile_size, rows * tile_size), (0, 0, 0, 0))

    for index, (_sprite_id, image) in enumerate(images):
        col = index % columns
        row = index // columns
        x = col * tile_size
        y = row * tile_size
        if image.size != (tile_size, tile_size):
            image = image.resize((tile_size, tile_size), Image.Resampling.NEAREST)
        sheet.alpha_composite(image, (x, y))

    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract selected sprites from Tibia.spr into PNG files.")
    parser.add_argument("--spr", required=True, help="Path to Tibia.spr")
    parser.add_argument("--ids", required=True, help="Comma-separated ids or ranges, ex: 724,1030-1040")
    parser.add_argument("--out", required=True, help="Output directory for extracted PNGs")
    parser.add_argument("--sprite-size", type=int, default=32, help="Sprite size, default: 32")
    parser.add_argument("--u32-count", action="store_true", help="Use U32 sprite count header")
    parser.add_argument("--alpha-channel", action="store_true", help="Treat sprite payload as RGBA")
    parser.add_argument("--sheet", help="Optional output path for a contact sheet PNG")
    parser.add_argument("--sheet-columns", type=int, default=12, help="Columns in optional contact sheet")
    args = parser.parse_args()

    spr_path = Path(args.spr)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    extracted: list[tuple[int, Image.Image]] = []
    for sprite_id in parse_ids(args.ids):
        image = decode_sprite(spr_path, sprite_id, args.sprite_size, args.u32_count, args.alpha_channel)
        if image is None:
            print(f"[skip] sprite {sprite_id} not found or empty")
            continue
        image.save(out_dir / f"{sprite_id}.png")
        extracted.append((sprite_id, image))
        print(f"[ok] extracted sprite {sprite_id}")

    if args.sheet:
        save_sheet(extracted, Path(args.sheet), args.sheet_columns, args.sprite_size)
        print(f"[ok] wrote sheet to {args.sheet}")


if __name__ == "__main__":
    main()
