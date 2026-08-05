#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from PIL import Image

from build_rme_native_border_mosaic import (
    BORDER_VALUES,
    TYPE_TO_EDGE,
    load_global_borders,
    read_otb_mapping,
)
from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


EDGE_DIRECTIONS = {
    "n": BORDER_VALUES["NORTH_HORIZONTAL"],
    "e": BORDER_VALUES["EAST_HORIZONTAL"],
    "s": BORDER_VALUES["SOUTH_HORIZONTAL"],
    "w": BORDER_VALUES["WEST_HORIZONTAL"],
    "cnw": BORDER_VALUES["NORTHWEST_CORNER"],
    "cne": BORDER_VALUES["NORTHEAST_CORNER"],
    "csw": BORDER_VALUES["SOUTHWEST_CORNER"],
    "cse": BORDER_VALUES["SOUTHEAST_CORNER"],
}

OPPOSITE_CORNER = {
    "cnw": "cse",
    "cne": "csw",
    "csw": "cne",
    "cse": "cnw",
}


def fill_missing(values: list[int | None]) -> list[int]:
    known = [index for index, value in enumerate(values) if value is not None]
    if not known:
        raise ValueError("Border sprite has no visible pixels")
    result = []
    for index, value in enumerate(values):
        if value is not None:
            result.append(value)
            continue
        nearest = min(known, key=lambda candidate: abs(candidate - index))
        result.append(values[nearest])
    return result


def clip_ground_to_edge(
    ground: Image.Image,
    border: Image.Image,
    edge: str,
) -> Image.Image:
    alpha = border.getchannel("A")
    result = Image.new("RGBA", ground.size, (0, 0, 0, 0))
    width, height = ground.size

    if edge in ("n", "s"):
        boundaries: list[int | None] = []
        for x in range(width):
            visible = [y for y in range(height) if alpha.getpixel((x, y)) > 0]
            if not visible:
                boundaries.append(None)
            elif edge == "n":
                boundaries.append(max(visible))
            else:
                boundaries.append(min(visible))
        for x, boundary in enumerate(fill_missing(boundaries)):
            if edge == "n":
                result.paste(ground.crop((x, boundary + 1, x + 1, height)), (x, boundary + 1))
            else:
                result.paste(ground.crop((x, 0, x + 1, boundary)), (x, 0))
    else:
        boundaries = []
        for y in range(height):
            visible = [x for x in range(width) if alpha.getpixel((x, y)) > 0]
            if not visible:
                boundaries.append(None)
            elif edge == "w":
                boundaries.append(max(visible))
            else:
                boundaries.append(min(visible))
        for y, boundary in enumerate(fill_missing(boundaries)):
            if edge == "w":
                result.paste(ground.crop((boundary + 1, y, width, y + 1)), (boundary + 1, y))
            else:
                result.paste(ground.crop((0, y, boundary, y + 1)), (0, y))

    return result


def cut_edge_tile(ground: Image.Image, border: Image.Image, edge: str) -> Image.Image:
    result = clip_ground_to_edge(ground, border, edge)
    result.alpha_composite(border)
    return result


def keep_light_border(
    border: Image.Image,
    luminance_threshold: float,
    saturation_threshold: float,
) -> Image.Image:
    result = Image.new("RGBA", border.size, (0, 0, 0, 0))
    source = border.load()
    target = result.load()
    for y in range(border.height):
        for x in range(border.width):
            red, green, blue, alpha = source[x, y]
            if alpha == 0:
                continue
            luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
            maximum = max(red, green, blue)
            minimum = min(red, green, blue)
            saturation = 0 if maximum == 0 else (maximum - minimum) / maximum
            if luminance >= luminance_threshold and saturation <= saturation_threshold:
                target[x, y] = (red, green, blue, alpha)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a clean square ground mosaic with an irregular RME border cutout."
    )
    parser.add_argument("--ground-client-id", type=int, required=True)
    parser.add_argument("--border-id", type=int, required=True)
    parser.add_argument("--rme-root", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--grid", type=int, default=12)
    parser.add_argument("--padding", type=int, default=32)
    parser.add_argument("--light-border-only", action="store_true")
    parser.add_argument("--light-luminance", type=float, default=82)
    parser.add_argument("--light-saturation", type=float, default=0.64)
    args = parser.parse_args()

    if args.grid < 3:
        raise ValueError("Grid must be at least 3x3")

    data_dir = args.rme_root / "data" / str(args.version)
    border = load_global_borders(data_dir / "borders.xml").get(args.border_id)
    if border is None:
        raise ValueError(f"RME border not found: {args.border_id}")

    server_to_client = read_otb_mapping(data_dir / "items.otb")
    edge_clients = {}
    for edge, direction in EDGE_DIRECTIONS.items():
        server_id = border.tiles.get(direction)
        if server_id is None or server_id not in server_to_client:
            raise ValueError(f"Border {args.border_id} has no mapped {edge} item")
        edge_clients[edge] = server_to_client[server_id]

    wanted = {args.ground_client_id, *edge_clients.values()}
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, wanted)
    ground = things[args.ground_client_id]

    args.out_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True, exist_ok=True)
    cache: dict[int, Image.Image] = {}

    def sprite(sprite_id: int) -> Image.Image:
        if sprite_id not in cache:
            image = decode_sprite(args.spr, sprite_id, 32, False, False)
            if image is None:
                raise ValueError(f"Unable to decode sprite {sprite_id}")
            cache[sprite_id] = image.convert("RGBA")
            cache[sprite_id].save(source_dir / f"{sprite_id}.png")
        return cache[sprite_id]

    def ground_sprite_id(x: int, y: int) -> int:
        px = x % ground.pattern_x
        py = y % ground.pattern_y
        return int(ground.sprites[py * ground.pattern_x + px])

    edge_sprites = {
        edge: int(things[client_id].sprites[0])
        for edge, client_id in edge_clients.items()
    }
    tile_size = 32
    offset = args.padding
    mosaic = Image.new(
        "RGBA",
        (
            args.grid * tile_size + args.padding * 2,
            args.grid * tile_size + args.padding * 2,
        ),
        (0, 0, 0, 0),
    )
    placements = []

    for y in range(args.grid):
        for x in range(args.grid):
            ground_id = ground_sprite_id(x, y)
            ground_tile = sprite(ground_id)
            edge = None
            if x == 0 and y == 0:
                edge = "cnw"
            elif x == args.grid - 1 and y == 0:
                edge = "cne"
            elif x == 0 and y == args.grid - 1:
                edge = "csw"
            elif x == args.grid - 1 and y == args.grid - 1:
                edge = "cse"
            elif y == 0:
                edge = "n"
            elif x == args.grid - 1:
                edge = "e"
            elif y == args.grid - 1:
                edge = "s"
            elif x == 0:
                edge = "w"

            if edge is None:
                tile = ground_tile
                border_id = None
            else:
                source_edge = OPPOSITE_CORNER.get(edge, edge)
                border_id = edge_sprites[source_edge]
                border_tile = sprite(border_id)
                if edge in ("n", "e", "s", "w"):
                    tile = clip_ground_to_edge(ground_tile, border_tile, edge)
                else:
                    horizontal = "n" if edge in ("cnw", "cne") else "s"
                    vertical = "w" if edge in ("cnw", "csw") else "e"
                    tile = clip_ground_to_edge(
                        ground_tile,
                        sprite(edge_sprites[horizontal]),
                        horizontal,
                    )
                    tile = clip_ground_to_edge(
                        tile,
                        sprite(edge_sprites[vertical]),
                        vertical,
                    )
                if args.light_border_only:
                    border_tile = keep_light_border(
                        border_tile,
                        args.light_luminance,
                        args.light_saturation,
                    )
                tile.alpha_composite(border_tile)

            mosaic.alpha_composite(
                tile,
                (offset + x * tile_size, offset + y * tile_size),
            )
            placements.append(
                {
                    "tileX": x,
                    "tileY": y,
                    "groundSpriteId": ground_id,
                    "edge": edge,
                    "sourceEdge": source_edge if edge is not None else None,
                    "borderSpriteId": border_id,
                }
            )

    treatment = "-ground-matched" if args.light_border_only else ""
    stem = (
        f"client-{args.ground_client_id}-border-{args.border_id}"
        f"-square-{args.grid}x{args.grid}{treatment}"
    )
    mosaic_path = args.out_dir / f"{stem}.png"
    preview_path = args.out_dir / f"{stem}-black-preview.png"
    mosaic.save(mosaic_path)

    preview = Image.new("RGBA", mosaic.size, (0, 0, 0, 255))
    preview.alpha_composite(mosaic)
    preview.convert("RGB").save(preview_path)

    manifest = {
        "type": "square-border-cutout-mosaic-v1",
        "groundClientId": args.ground_client_id,
        "borderId": args.border_id,
        "grid": args.grid,
        "tileSize": tile_size,
        "padding": args.padding,
        "lightBorderOnly": args.light_border_only,
        "lightLuminance": args.light_luminance if args.light_border_only else None,
        "lightSaturation": args.light_saturation if args.light_border_only else None,
        "mosaic": str(mosaic_path.resolve()),
        "preview": str(preview_path.resolve()),
        "groundSpriteIds": ground.unique_sprites,
        "edgeClientIds": edge_clients,
        "edgeSpriteIds": edge_sprites,
        "placements": placements,
    }
    manifest_path = args.out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
