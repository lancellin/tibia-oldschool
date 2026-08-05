#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


TILE_SIZE = 32
TARGET_CLIENT_IDS = (1966, 1967)


def render_item(thing, spr_path: Path) -> Image.Image:
    image = Image.new(
        "RGBA",
        (thing.width * TILE_SIZE, thing.height * TILE_SIZE),
        (0, 0, 0, 0),
    )
    for index, sprite_id in enumerate(thing.sprites[: thing.width * thing.height]):
        if sprite_id <= 0:
            continue
        sprite = decode_sprite(spr_path, int(sprite_id), TILE_SIZE, False, False)
        if sprite is None:
            raise ValueError(f"Unable to decode sprite {sprite_id}")
        x = (index % thing.width) * TILE_SIZE
        y = (index // thing.width) * TILE_SIZE
        image.alpha_composite(sprite.convert("RGBA"), (x, y))
    return image


def draw_facade_target(
    canvas: Image.Image,
    images: dict[int, Image.Image],
    panel_top: int,
    panel_height: int,
    target_client_id: int,
) -> tuple[int, int]:
    x_positions = list(range(-TILE_SIZE, canvas.width, TILE_SIZE))
    row_positions = list(
        range(panel_top + TILE_SIZE, panel_top + panel_height - TILE_SIZE * 2, TILE_SIZE * 2)
    )
    target_x = x_positions[len(x_positions) // 2]
    target_y = row_positions[len(row_positions) // 2]

    # Each 2x2 object occupies 64px vertically. Repeating at this stride gives
    # the upscaler real context across the horizontal boundaries seen in-game.
    for y in row_positions:
        for x in x_positions:
            canvas.alpha_composite(images[1967], (x, y))
        for x in x_positions:
            canvas.alpha_composite(images[1966], (x, y))

    # Redraw only the selected target so its pixels can be safely recut while
    # still seeing identical rows above, below, left and right.
    canvas.alpha_composite(images[target_client_id], (target_x, target_y))
    return target_x, target_y


def checkerboard(size: tuple[int, int], cell: int = 16) -> Image.Image:
    image = Image.new("RGBA", size, (42, 42, 42, 255))
    draw = ImageDraw.Draw(image)
    colors = ((45, 45, 45, 255), (65, 65, 65, 255))
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            draw.rectangle(
                (x, y, x + cell - 1, y + cell - 1),
                fill=colors[((x // cell) + (y // cell)) % 2],
            )
    return image


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a lossless continuity atlas for the south/front pyramid face."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    args = parser.parse_args()

    if args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    source_dir = args.out_dir / "source-tiles"
    source_dir.mkdir(parents=True)

    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, set(TARGET_CLIENT_IDS))
    images = {client_id: render_item(things[client_id], args.spr) for client_id in TARGET_CLIENT_IDS}

    for client_id, image in images.items():
        image.save(source_dir / f"client-{client_id}-object.png")
        thing = things[client_id]
        for sprite_id in thing.unique_sprites:
            sprite = decode_sprite(args.spr, int(sprite_id), TILE_SIZE, False, False)
            if sprite is None:
                raise ValueError(f"Unable to decode sprite {sprite_id}")
            sprite.convert("RGBA").save(source_dir / f"{sprite_id}.png")

    atlas = Image.new("RGBA", (24 * TILE_SIZE, 40 * TILE_SIZE), (0, 0, 0, 0))
    panel_height = 20 * TILE_SIZE
    target_placements = []

    target_1966 = draw_facade_target(atlas, images, 0, panel_height, 1966)
    target_placements.append(
        {
            "clientId": 1966,
            "object": {"x": target_1966[0], "y": target_1966[1], "width": 64, "height": 64},
            "spriteIds": [int(value) for value in things[1966].sprites[:4]],
        }
    )

    target_1967 = draw_facade_target(atlas, images, panel_height, panel_height, 1967)
    target_placements.append(
        {
            "clientId": 1967,
            "object": {"x": target_1967[0], "y": target_1967[1], "width": 64, "height": 64},
            "spriteIds": [int(value) for value in things[1967].sprites[:4]],
        }
    )

    input_path = args.out_dir / "pyramid-south-face-1966-1967-atlas-input.png"
    atlas.save(input_path)

    preview = checkerboard(atlas.size)
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    labels = (
        "CID 1966 target inside full vertical facade",
        "CID 1967 target inside full vertical facade",
    )
    for index, label in enumerate(labels):
        y = index * panel_height
        draw.rectangle((0, y, atlas.width - 1, y + panel_height - 1), outline=(220, 220, 220, 180))
        draw.text((6, y + 4), label, fill=(255, 255, 255, 255))
    for target in target_placements:
        box = target["object"]
        draw.rectangle(
            (
                int(box["x"]),
                int(box["y"]),
                int(box["x"]) + int(box["width"]) - 1,
                int(box["y"]) + int(box["height"]) - 1,
            ),
            outline=(255, 64, 64, 255),
            width=2,
        )
    preview_path = args.out_dir / "pyramid-south-face-1966-1967-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "pyramid-south-face-full-facade-continuity-atlas",
        "version": args.version,
        "tileSize": TILE_SIZE,
        "atlasSize": {"width": atlas.width, "height": atlas.height},
        "expectedUpscale2x": {"width": atlas.width * 2, "height": atlas.height * 2},
        "finalScale": 2,
        "clientIds": list(TARGET_CLIENT_IDS),
        "targetSpriteIds": sorted(
            {
                int(sprite_id)
                for client_id in TARGET_CLIENT_IDS
                for sprite_id in things[client_id].unique_sprites
            }
        ),
        "targetPlacements": target_placements,
        "input": str(input_path),
        "preview": str(preview_path),
        "notes": [
            "CID 1966 and CID 1967 are 2x2 objects with four source sprites each.",
            "Each target has repeated 2x2 facade rows above and below at a 64px source stride.",
            "The two panels redraw the selected target last so all target pixels survive recutting.",
            "Original sprite alpha must be restored after recutting to remove contextual pixels.",
            "This layout specifically prevents horizontal lines caused by upscaling isolated rows.",
        ],
    }
    manifest_path = args.out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    readme = (
        "UPSCALE ONLY pyramid-south-face-1966-1967-atlas-input.png\n"
        "Expected 2x output size: 1536x2560.\n"
        "Do not upscale the preview image.\n"
        "CID 1966: sprites 4580-4583. CID 1967: sprites 4576-4579.\n"
    )
    (args.out_dir / "_LEIA-ME.txt").write_text(readme, encoding="ascii")

    print(
        json.dumps(
            {
                "outDir": str(args.out_dir),
                "input": str(input_path),
                "preview": str(preview_path),
                "manifest": str(manifest_path),
                "targetSpriteIds": manifest["targetSpriteIds"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
