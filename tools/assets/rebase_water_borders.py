#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


class SprReader:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        self.count = struct.unpack_from("<H", self.data, 4)[0]

    def decode(self, sprite_id: int) -> Image.Image:
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


def is_old_blue(r: int, g: int, b: int, alpha: int) -> bool:
    if alpha == 0 or b < 65:
        return False
    maximum = max(r, g, b)
    minimum = min(r, g, b)
    saturation = 0 if maximum == 0 else (maximum - minimum) / maximum
    return saturation >= 0.28 and b > r * 1.35 and b > g * 1.12


def remove_old_water(image: Image.Image) -> tuple[Image.Image, Image.Image]:
    output = image.copy().convert("RGBA")
    mask = Image.new("L", output.size, 0)
    pixels = output.load()
    mask_pixels = mask.load()
    for y in range(output.height):
        for x in range(output.width):
            r, g, b, alpha = pixels[x, y]
            if is_old_blue(r, g, b, alpha):
                pixels[x, y] = (0, 0, 0, 0)
                mask_pixels[x, y] = 255
    return output, mask


def replace_only_old_water(
    original: Image.Image,
    preserved_overlay: Image.Image,
    old_water_mask: Image.Image,
    new_water: Image.Image,
) -> Image.Image:
    # Keep original transparent pixels transparent; fill only pixels that were old blue water.
    output = preserved_overlay.copy()
    output.paste(new_water, (0, 0), old_water_mask)
    return output


def sprite_index(pattern_x: int, pattern_y: int, phase: int, px: int, py: int) -> int:
    return phase * pattern_x * pattern_y + py * pattern_x + px


def save_sheet(images: list[tuple[str, Image.Image]], output: Path, columns: int = 16) -> None:
    rows = math.ceil(len(images) / columns)
    cell_width = 48
    cell_height = 46
    sheet = Image.new("RGBA", (columns * cell_width, rows * cell_height), (28, 28, 28, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(images):
        x = (index % columns) * cell_width
        y = (index // columns) * cell_height
        sheet.alpha_composite(image, (x + 8, y))
        draw.text((x + 2, y + 33), label, fill=(225, 225, 225, 255))
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Remove legacy blue water from border sprites and rebase them on exact 7.4 water phases."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--otb", type=Path, required=True)
    parser.add_argument("--server-start", type=int, default=4632)
    parser.add_argument("--server-end", type=int, default=4663)
    parser.add_argument("--water-client-id", type=int, default=4597)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    server_to_client = read_otb_mapping(args.otb)
    mappings = [
        (server_id, server_to_client[server_id])
        for server_id in range(args.server_start, args.server_end + 1)
    ]
    wanted = {args.water_client_id, *(client_id for _, client_id in mappings)}
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, wanted)
    water = things[args.water_client_id]
    if (water.pattern_x, water.pattern_y, water.frames) != (4, 2, 16):
        raise ValueError("Expected the patched water layout 4x2 patterns and 16 frames")

    spr = SprReader(args.spr)
    water_images = [spr.decode(sprite_id) for sprite_id in water.sprites]
    args.output.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []

    for server_id, client_id in mappings:
        thing = things[client_id]
        item_dir = args.output / f"server-{server_id}_client-{client_id}"
        overlay_dir = item_dir / "transparent-overlays-original-order"
        review_dir = item_dir / "review-after-original-order"
        composite_dir = item_dir / "rebased-4x2x16"
        overlay_dir.mkdir(parents=True, exist_ok=True)
        review_dir.mkdir(parents=True, exist_ok=True)
        composite_dir.mkdir(parents=True, exist_ok=True)

        originals: list[Image.Image] = []
        overlays: list[Image.Image] = []
        water_masks: list[Image.Image] = []
        overlay_sheet: list[tuple[str, Image.Image]] = []
        for index, sprite_id in enumerate(thing.sprites):
            original = spr.decode(sprite_id)
            overlay, water_mask = remove_old_water(original)
            originals.append(original)
            overlays.append(overlay)
            water_masks.append(water_mask)
            overlay.save(overlay_dir / f"overlay-{index:03d}-sprite-{sprite_id}.png")
            water_mask.save(overlay_dir / f"water-mask-{index:03d}-sprite-{sprite_id}.png")
            overlay_sheet.append((f"{index:02d}", overlay))

        review_sheet: list[tuple[str, Image.Image]] = []
        patterns_per_phase = thing.pattern_x * thing.pattern_y
        for index, sprite_id in enumerate(thing.sprites):
            old_phase = index // patterns_per_phase
            pattern_index = index % patterns_per_phase
            old_py = pattern_index // thing.pattern_x
            old_px = pattern_index % thing.pattern_x
            water_phase = min(15, old_phase * 16 // thing.frames)
            water_px = old_px % 4
            water_py = old_py % 2
            water_index = sprite_index(4, 2, water_phase, water_px, water_py)
            review = replace_only_old_water(
                originals[index],
                overlays[index],
                water_masks[index],
                water_images[water_index],
            )
            review.save(review_dir / f"after-{index:03d}-sprite-{sprite_id}.png")
            review_sheet.append((f"{index:02d}", review))

        composites: list[tuple[str, Image.Image]] = []
        output_refs: list[dict[str, int]] = []
        output_index = 0
        for phase in range(16):
            old_phase = min(thing.frames - 1, phase * thing.frames // 16)
            for py in range(2):
                old_py = py % thing.pattern_y
                for px in range(4):
                    old_px = px % thing.pattern_x
                    water_index = sprite_index(4, 2, phase, px, py)
                    overlay_index = sprite_index(
                        thing.pattern_x,
                        thing.pattern_y,
                        old_phase,
                        old_px,
                        old_py,
                    )
                    composed = replace_only_old_water(
                        originals[overlay_index],
                        overlays[overlay_index],
                        water_masks[overlay_index],
                        water_images[water_index],
                    )
                    filename = (
                        f"frame-{output_index:03d}_phase-{phase:02d}"
                        f"_px-{px}_py-{py}_old-{overlay_index:02d}.png"
                    )
                    composed.save(composite_dir / filename)
                    composites.append((f"{phase:02d}:{px}{py}", composed))
                    output_refs.append(
                        {
                            "index": output_index,
                            "phase": phase,
                            "patternX": px,
                            "patternY": py,
                            "waterSpriteId": water.sprites[water_index],
                            "sourceOverlayIndex": overlay_index,
                            "sourceBorderSpriteId": thing.sprites[overlay_index],
                        }
                    )
                    output_index += 1

        save_sheet(overlay_sheet, item_dir / "transparent-overlays-sheet.png", columns=16)
        save_sheet(review_sheet, item_dir / "review-after-original-order-sheet.png", columns=16)
        save_sheet(composites, item_dir / "rebased-4x2x16-sheet.png", columns=16)
        metadata = {
            "serverId": server_id,
            "clientId": client_id,
            "sourceLayout": {
                "patternX": thing.pattern_x,
                "patternY": thing.pattern_y,
                "frames": thing.frames,
                "references": len(thing.sprites),
            },
            "targetLayout": {
                "patternX": 4,
                "patternY": 2,
                "frames": 16,
                "references": 128,
            },
            "sourceSpriteIds": thing.sprites,
            "outputReferences": output_refs,
        }
        (item_dir / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        manifest.append(metadata)
        print(f"[ok] server {server_id}: {len(thing.sprites)} overlays -> 128 synchronized composites")

    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[ok] wrote {args.output / 'manifest.json'}")


if __name__ == "__main__":
    main()
