#!/usr/bin/env python3
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw


class SpriteArchive:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.signature = self.data[:4].hex()
        self.count = struct.unpack_from("<H", self.data, 4)[0]
        self.addresses = [
            struct.unpack_from("<I", self.data, 6 + index * 4)[0]
            for index in range(self.count)
        ]

    def decode(self, sprite_id: int) -> Image.Image | None:
        address = self.addresses[sprite_id - 1]
        if address == 0:
            return None
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


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def folder_name(start: int, end: int) -> str:
    return f"{start:04d}-{end:04d}"


def save_sheet(images: list[tuple[int, Image.Image]], output: Path, columns: int = 20) -> None:
    if not images:
        return
    rows = math.ceil(len(images) / columns)
    cell_width = 48
    cell_height = 46
    sheet = Image.new("RGBA", (columns * cell_width, rows * cell_height), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (sprite_id, image) in enumerate(images):
        x = (index % columns) * cell_width
        y = (index // columns) * cell_height
        sheet.alpha_composite(image, (x + 8, y))
        draw.text((x + 1, y + 33), str(sprite_id), fill=(225, 225, 225, 255))
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract an entire classic Tibia SPR into browsable folders and sheets.")
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--folder-size", type=int, default=1000)
    parser.add_argument("--sheet-size", type=int, default=500)
    args = parser.parse_args()

    archive = SpriteArchive(args.spr)
    args.output.mkdir(parents=True, exist_ok=True)
    sheets_dir = args.output / "sheets"
    sheets_dir.mkdir(parents=True, exist_ok=True)
    extracted_ids: list[int] = []
    empty_ids: list[int] = []
    sheet_images: list[tuple[int, Image.Image]] = []
    current_sheet_start = 1

    for sprite_id in range(1, archive.count + 1):
        folder_start = ((sprite_id - 1) // args.folder_size) * args.folder_size + 1
        folder_end = min(folder_start + args.folder_size - 1, archive.count)
        folder = args.output / folder_name(folder_start, folder_end)
        folder.mkdir(parents=True, exist_ok=True)

        image = archive.decode(sprite_id)
        if image is None:
            empty_ids.append(sprite_id)
        else:
            image.save(folder / f"{sprite_id}.png")
            extracted_ids.append(sprite_id)
            sheet_images.append((sprite_id, image))

        sheet_end = min(current_sheet_start + args.sheet_size - 1, archive.count)
        if sprite_id == sheet_end:
            save_sheet(
                sheet_images,
                sheets_dir / f"sprites-{current_sheet_start:04d}-{sheet_end:04d}.png",
            )
            print(
                f"[ok] {current_sheet_start:04d}-{sheet_end:04d}: "
                f"{len(sheet_images)} sprites"
            )
            sheet_images = []
            current_sheet_start = sheet_end + 1

    manifest = {
        "source": str(args.spr),
        "sourceSha256": sha256(args.spr),
        "signature": archive.signature,
        "spriteSlots": archive.count,
        "extractedPngCount": len(extracted_ids),
        "emptySlotCount": len(empty_ids),
        "emptySpriteIds": empty_ids,
        "folderSize": args.folder_size,
        "sheetSize": args.sheet_size,
        "spriteSize": [32, 32],
        "format": "RGBA PNG",
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (args.output / "README.txt").write_text(
        "Tibia 7.72 classic sprite archive\n"
        f"Source slots: {archive.count}\n"
        f"Extracted PNGs: {len(extracted_ids)}\n"
        f"Empty slots omitted: {len(empty_ids)}\n"
        "Folders contain groups of 1000 sprite IDs. Sheets contain groups of 500 IDs.\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
