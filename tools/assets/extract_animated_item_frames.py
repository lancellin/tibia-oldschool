#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


def parse_ids(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start_text, end_text = part.split("-", 1)
            result.extend(range(int(start_text), int(end_text) + 1))
        else:
            result.append(int(part))
    return result


class SprReader:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        self.count = struct.unpack_from("<H", self.data, 4)[0]

    def decode(self, sprite_id: int) -> Image.Image:
        if sprite_id <= 0 or sprite_id > self.count:
            raise ValueError(f"Sprite ID {sprite_id} is outside 1..{self.count}")
        address = struct.unpack_from("<I", self.data, 6 + (sprite_id - 1) * 4)[0]
        if address == 0:
            return Image.new("RGBA", (32, 32), (0, 0, 0, 0))

        payload_size = struct.unpack_from("<H", self.data, address + 3)[0]
        payload = self.data[address + 5:address + 5 + payload_size]
        image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        pixels = image.load()
        cursor = 0
        pixel_index = 0

        while cursor + 4 <= len(payload) and pixel_index < 1024:
            transparent, colored = struct.unpack_from("<HH", payload, cursor)
            cursor += 4
            pixel_index += transparent
            for _ in range(colored):
                if cursor + 3 > len(payload) or pixel_index >= 1024:
                    break
                x = pixel_index % 32
                y = pixel_index // 32
                r, g, b = struct.unpack_from("<BBB", payload, cursor)
                pixels[x, y] = (r, g, b, 255)
                cursor += 3
                pixel_index += 1
        return image


def save_sheet(frames: list[tuple[str, Image.Image]], output: Path, columns: int = 8) -> None:
    rows = math.ceil(len(frames) / columns)
    cell_height = 48
    sheet = Image.new("RGBA", (columns * 64, rows * cell_height), (28, 28, 28, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(frames):
        x = (index % columns) * 64
        y = (index // columns) * cell_height
        sheet.alpha_composite(image, (x + 16, y))
        draw.text((x + 2, y + 33), label, fill=(230, 230, 230, 255))
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract every ordered frame/pattern reference for a range of server item IDs."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--otb", type=Path, required=True)
    parser.add_argument("--server-ids", required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    server_ids = parse_ids(args.server_ids)
    server_to_client = read_otb_mapping(args.otb)
    mappings = [(server_id, server_to_client[server_id]) for server_id in server_ids]
    client_ids = {client_id for _, client_id in mappings}
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, client_ids)
    spr = SprReader(args.spr)
    args.output.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []

    for server_id, client_id in mappings:
        thing = things[client_id]
        item_dir = args.output / f"server-{server_id}_client-{client_id}"
        frames_dir = item_dir / "ordered-frames"
        unique_dir = item_dir / "unique-sprites"
        frames_dir.mkdir(parents=True, exist_ok=True)
        unique_dir.mkdir(parents=True, exist_ok=True)

        ordered_images: list[tuple[str, Image.Image]] = []
        for index, sprite_id in enumerate(thing.sprites):
            image = spr.decode(sprite_id)
            label = f"{index:03d}-{sprite_id}"
            image.save(frames_dir / f"frame-{label}.png")
            ordered_images.append((label, image))

        for sprite_id in thing.unique_sprites:
            spr.decode(sprite_id).save(unique_dir / f"sprite-{sprite_id}.png")

        save_sheet(ordered_images, item_dir / "ordered-frames-sheet.png")
        metadata = {
            "serverId": server_id,
            "clientId": client_id,
            "layout": {
                "width": thing.width,
                "height": thing.height,
                "layers": thing.layers,
                "patternX": thing.pattern_x,
                "patternY": thing.pattern_y,
                "patternZ": thing.pattern_z,
                "frames": thing.frames,
            },
            "orderedSpriteIds": thing.sprites,
            "uniqueSpriteIds": thing.unique_sprites,
            "orderedFrameCount": len(thing.sprites),
            "uniqueSpriteCount": len(thing.unique_sprites),
        }
        (item_dir / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        manifest.append(metadata)
        print(f"[ok] server {server_id} -> client {client_id}: {len(thing.sprites)} ordered frames")

    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[ok] wrote {args.output / 'manifest.json'}")


if __name__ == "__main__":
    main()
