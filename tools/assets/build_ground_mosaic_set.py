#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


class SpriteArchive:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        self.count = struct.unpack_from("<H", self.data, 4)[0]
        self.addresses = [
            struct.unpack_from("<I", self.data, 6 + index * 4)[0]
            for index in range(self.count)
        ]

    def decode(self, sprite_id: int) -> Image.Image:
        if sprite_id <= 0:
            return Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        address = self.addresses[sprite_id - 1]
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


def parse_ids(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if part:
            result.append(int(part))
    return result


def sprite_index(
    width: int,
    height: int,
    layers: int,
    pattern_x: int,
    pattern_y: int,
    pattern_z: int,
    frame: int,
    z: int,
    py: int,
    px: int,
    layer: int,
    sy: int = 0,
    sx: int = 0,
) -> int:
    index = frame
    index = index * pattern_z + z
    index = index * pattern_y + py
    index = index * pattern_x + px
    index = index * layers + layer
    index = index * height + sy
    index = index * width + sx
    return index


def composite_variant(thing, archive: SpriteArchive, frame: int, z: int, px: int, py: int) -> tuple[Image.Image, list[int]]:
    tile = Image.new("RGBA", (thing.width * 32, thing.height * 32), (0, 0, 0, 0))
    sprite_ids: list[int] = []
    for layer in range(thing.layers):
        for sy in range(thing.height):
            for sx in range(thing.width):
                index = sprite_index(
                    thing.width,
                    thing.height,
                    thing.layers,
                    thing.pattern_x,
                    thing.pattern_y,
                    thing.pattern_z,
                    frame,
                    z,
                    py,
                    px,
                    layer,
                    sy,
                    sx,
                )
                sprite_id = thing.sprites[index]
                sprite_ids.append(sprite_id)
                tile.alpha_composite(archive.decode(sprite_id), (sx * 32, sy * 32))
    return tile, sprite_ids


def save_variant_sheet(
    variants: list[tuple[str, Image.Image]],
    output: Path,
    columns: int = 8,
) -> None:
    if not variants:
        return
    rows = math.ceil(len(variants) / columns)
    cell_w = 64
    cell_h = 48
    sheet = Image.new("RGBA", (columns * cell_w, rows * cell_h), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(variants):
        x = (index % columns) * cell_w
        y = (index // columns) * cell_h
        sheet.alpha_composite(image, (x + 16, y))
        draw.text((x + 2, y + 34), label, fill=(225, 225, 225, 255))
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build 24x24 ground mosaics from DAT client IDs.")
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--otb", type=Path)
    parser.add_argument("--server-ids")
    parser.add_argument("--client-ids")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--grid", type=int, default=24)
    args = parser.parse_args()

    server_ids = parse_ids(args.server_ids or "")
    direct_client_ids = parse_ids(args.client_ids or "")
    if not server_ids and not direct_client_ids:
        parser.error("at least one of --server-ids or --client-ids is required")
    if server_ids and not args.otb:
        parser.error("--otb is required when --server-ids is used")

    entries: list[tuple[str, int, int]] = []
    if server_ids:
        server_to_client = read_otb_mapping(args.otb)
        missing = [server_id for server_id in server_ids if server_id not in server_to_client]
        if missing:
            parser.error(f"server IDs missing from OTB: {missing}")
        entries.extend(("server", server_id, server_to_client[server_id]) for server_id in server_ids)
    entries.extend(("client", client_id, client_id) for client_id in direct_client_ids)

    client_ids = sorted({client_id for _source, _requested_id, client_id in entries})
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, set(client_ids))
    archive = SpriteArchive(args.spr)
    args.output.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []

    for source, requested_id, client_id in entries:
        thing = things[client_id]
        item_label = f"sid-{requested_id}-cid-{client_id}" if source == "server" else f"cid-{client_id}"
        item_dir = args.output / item_label
        source_dir = item_dir / "source-composited-tiles"
        item_dir.mkdir(parents=True, exist_ok=True)
        source_dir.mkdir(parents=True, exist_ok=True)
        variants: dict[tuple[int, int, int, int], Image.Image] = {}
        variant_refs: dict[str, list[int]] = {}
        sheet_variants: list[tuple[str, Image.Image]] = []

        for frame in range(thing.frames):
            for z in range(thing.pattern_z):
                for py in range(thing.pattern_y):
                    for px in range(thing.pattern_x):
                        image, refs = composite_variant(thing, archive, frame, z, px, py)
                        key = (frame, z, px, py)
                        variants[key] = image
                        label = f"f{frame:02d}-z{z}-x{px}-y{py}"
                        image.save(source_dir / f"{label}.png")
                        variant_refs[label] = refs
                        sheet_variants.append((label, image))

        outputs: list[str] = []
        animation_frames: list[Image.Image] = []
        for frame in range(thing.frames):
            for z in range(thing.pattern_z):
                mosaic = Image.new("RGBA", (args.grid * 32, args.grid * 32), (0, 0, 0, 0))
                for gy in range(args.grid):
                    py = gy % thing.pattern_y
                    for gx in range(args.grid):
                        px = gx % thing.pattern_x
                        tile = variants[(frame, z, px, py)]
                        mosaic.alpha_composite(tile, (gx * 32, gy * 32))
                suffix = f"-frame-{frame:02d}" if thing.frames > 1 else ""
                suffix += f"-z-{z}" if thing.pattern_z > 1 else ""
                output_path = item_dir / f"{item_label}-mosaic-{args.grid}x{args.grid}-1x{suffix}.png"
                mosaic.save(output_path)
                outputs.append(str(output_path))
                if thing.pattern_z == 1:
                    animation_frames.append(mosaic)

        if thing.frames > 1 and thing.pattern_z == 1:
            animation_frames[0].save(
                item_dir / f"{item_label}-animation-preview.gif",
                save_all=True,
                append_images=animation_frames[1:],
                duration=180,
                loop=0,
                disposal=2,
            )

        save_variant_sheet(sheet_variants, item_dir / f"{item_label}-source-contact-sheet.png")
        metadata = {
            "sourceIdType": source,
            "requestedId": requested_id,
            "serverId": requested_id if source == "server" else None,
            "clientId": client_id,
            "isGround": 0 in thing.attrs,
            "layout": {
                "width": thing.width,
                "height": thing.height,
                "layers": thing.layers,
                "patternX": thing.pattern_x,
                "patternY": thing.pattern_y,
                "patternZ": thing.pattern_z,
                "frames": thing.frames,
            },
            "grid": [args.grid, args.grid],
            "tileSize": [32, 32],
            "mosaicSize": [args.grid * 32, args.grid * 32],
            "spriteReferences": thing.sprites,
            "variantReferences": variant_refs,
            "outputs": outputs,
        }
        (item_dir / "manifest.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        manifest.append(metadata)
        print(f"[ok] {item_label}: {len(outputs)} mosaic(s)")

    index = {
        "serverIds": server_ids,
        "directClientIds": direct_client_ids,
        "resolvedClientIds": client_ids,
        "grid": [args.grid, args.grid],
        "tileSize": [32, 32],
        "mosaicSize": [args.grid * 32, args.grid * 32],
        "items": manifest,
        "notes": [
            "RME item IDs are server IDs and are translated through items.otb before reading Tibia.dat.",
            "Ground border IDs 891-902 are intentionally excluded and will be resolved separately.",
            "Grass and dirt already approved in HD are intentionally excluded.",
            "Animated grounds are exported one mosaic per frame.",
            "Pattern-Z variants are exported as separate mosaics.",
        ],
    }
    (args.output / "index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")
    (args.output / "README.txt").write_text(
        "Tibia 7.72 ground mosaics for manual Upscayl\n"
        "Each mosaic is 24x24 tiles (768x768 pixels at 1x).\n"
        "RME server IDs are translated to client IDs through items.otb.\n"
        "Animated CIDs: one PNG per frame plus GIF preview.\n"
        "Pattern-Z variants: one PNG per Z value.\n"
        "Do not include borders 891-902 in these mosaics; they will be processed separately.\n"
        "Grass and dirt already approved in HD were not included.\n",
        encoding="utf-8",
    )
    print(json.dumps({"output": str(args.output), "resolvedClientIds": client_ids, "items": len(manifest)}, indent=2))


if __name__ == "__main__":
    main()
