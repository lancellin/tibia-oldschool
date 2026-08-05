#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw

from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat


TILE_SIZE = 32

FACE_SPECS = (
    {
        "name": "north-face-1964-1965",
        "primary": 1964,
        "extension": 1965,
        "pairOffset": (0, -1),
        "levelRecession": (0, -1),
        "tangent": (1, 0),
    },
    {
        "name": "east-face-1962-1963",
        "primary": 1962,
        "extension": 1963,
        "pairOffset": (1, 0),
        "levelRecession": (-1, 0),
        "tangent": (0, 1),
    },
    {
        "name": "west-face-1960-1961",
        "primary": 1960,
        "extension": 1961,
        "pairOffset": (-1, 0),
        "levelRecession": (1, 0),
        "tangent": (0, 1),
    },
)


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


def project(map_x: int, map_y: int, map_z: int) -> tuple[int, int]:
    floor_delta = -map_z
    return (
        (map_x - floor_delta) * TILE_SIZE,
        (map_y - floor_delta) * TILE_SIZE,
    )


def build_placements(spec: dict[str, object], levels: int, repeat: int) -> list[dict[str, int]]:
    placements: list[dict[str, int]] = []
    half = repeat // 2
    tangent_x, tangent_y = spec["tangent"]
    recession_x, recession_y = spec["levelRecession"]
    offset_x, offset_y = spec["pairOffset"]

    for level in range(levels):
        z = -level
        level_x = level * recession_x
        level_y = level * recession_y
        for tangent_index in range(-half, repeat - half):
            base_x = level_x + tangent_index * tangent_x
            base_y = level_y + tangent_index * tangent_y
            placements.append(
                {
                    "clientId": int(spec["extension"]),
                    "x": base_x + offset_x,
                    "y": base_y + offset_y,
                    "z": z,
                    "level": level,
                    "tangentIndex": tangent_index,
                    "role": 0,
                }
            )
            placements.append(
                {
                    "clientId": int(spec["primary"]),
                    "x": base_x,
                    "y": base_y,
                    "z": z,
                    "level": level,
                    "tangentIndex": tangent_index,
                    "role": 1,
                }
            )
    return placements


def render_panel(
    placements: list[dict[str, int]],
    images: dict[int, Image.Image],
    target_client_id: int,
    target_level: int,
) -> tuple[Image.Image, dict[str, int]]:
    projected = []
    target = None
    for placement in placements:
        screen_x, screen_y = project(placement["x"], placement["y"], placement["z"])
        row = {**placement, "screenX": screen_x, "screenY": screen_y}
        projected.append(row)
        if (
            placement["clientId"] == target_client_id
            and placement["level"] == target_level
            and placement["tangentIndex"] == 0
        ):
            target = row

    if target is None:
        raise ValueError(f"Target placement not found for CID {target_client_id}")

    min_x = min(row["screenX"] for row in projected)
    min_y = min(row["screenY"] for row in projected)
    max_x = max(row["screenX"] + images[row["clientId"]].width for row in projected)
    max_y = max(row["screenY"] + images[row["clientId"]].height for row in projected)
    pad = TILE_SIZE * 2

    panel = Image.new(
        "RGBA",
        (max_x - min_x + pad * 2, max_y - min_y + pad * 2),
        (0, 0, 0, 0),
    )
    for row in sorted(projected, key=lambda value: (-value["z"], value["role"])):
        x = row["screenX"] - min_x + pad
        y = row["screenY"] - min_y + pad
        panel.alpha_composite(images[row["clientId"]], (x, y))

    target_x = target["screenX"] - min_x + pad
    target_y = target["screenY"] - min_y + pad
    panel.alpha_composite(images[target_client_id], (target_x, target_y))
    return panel, {
        "x": target_x,
        "y": target_y,
        "width": images[target_client_id].width,
        "height": images[target_client_id].height,
    }


def checkerboard(size: tuple[int, int], cell: int = 16) -> Image.Image:
    image = Image.new("RGBA", size, (40, 40, 40, 255))
    draw = ImageDraw.Draw(image)
    colors = ((44, 44, 44, 255), (66, 66, 66, 255))
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            draw.rectangle(
                (x, y, x + cell - 1, y + cell - 1),
                fill=colors[((x // cell) + (y // cell)) % 2],
            )
    return image


def build_face(
    spec: dict[str, object],
    things: dict[int, object],
    images: dict[int, Image.Image],
    spr_path: Path,
    version: int,
    out_root: Path,
    levels: int,
    repeat: int,
) -> dict[str, object]:
    out_dir = out_root / str(spec["name"])
    source_dir = out_dir / "source-tiles"
    source_dir.mkdir(parents=True)

    client_ids = (int(spec["primary"]), int(spec["extension"]))
    for client_id in client_ids:
        images[client_id].save(source_dir / f"client-{client_id}-object.png")
        for sprite_id in things[client_id].unique_sprites:
            sprite = decode_sprite(spr_path, int(sprite_id), TILE_SIZE, False, False)
            if sprite is None:
                raise ValueError(f"Unable to decode sprite {sprite_id}")
            sprite.convert("RGBA").save(source_dir / f"{sprite_id}.png")

    placements = build_placements(spec, levels, repeat)
    target_level = levels // 2
    panels = []
    targets = []
    for client_id in client_ids:
        panel, box = render_panel(placements, images, client_id, target_level)
        panels.append((client_id, panel))
        targets.append(
            {
                "clientId": client_id,
                "object": box,
                "spriteIds": [
                    int(value)
                    for value in things[client_id].sprites[
                        : things[client_id].width * things[client_id].height
                    ]
                ],
                "size": {
                    "width": int(things[client_id].width),
                    "height": int(things[client_id].height),
                },
            }
        )

    gap = TILE_SIZE
    atlas_width = max(panel.width for _, panel in panels)
    atlas_height = sum(panel.height for _, panel in panels) + gap
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    panel_rows = []
    cursor_y = 0
    for index, (client_id, panel) in enumerate(panels):
        atlas.alpha_composite(panel, (0, cursor_y))
        targets[index]["object"]["y"] += cursor_y
        panel_rows.append(
            {
                "clientId": client_id,
                "x": 0,
                "y": cursor_y,
                "width": panel.width,
                "height": panel.height,
            }
        )
        cursor_y += panel.height + (gap if index == 0 else 0)

    input_path = out_dir / f"{spec['name']}-atlas-input.png"
    atlas.save(input_path)

    preview = checkerboard(atlas.size)
    preview.alpha_composite(atlas)
    draw = ImageDraw.Draw(preview)
    for panel_row, target in zip(panel_rows, targets):
        draw.rectangle(
            (
                panel_row["x"],
                panel_row["y"],
                panel_row["x"] + panel_row["width"] - 1,
                panel_row["y"] + panel_row["height"] - 1,
            ),
            outline=(220, 220, 220, 180),
        )
        draw.text(
            (panel_row["x"] + 5, panel_row["y"] + 4),
            f"CID {panel_row['clientId']} target",
            fill=(255, 255, 255, 255),
        )
        box = target["object"]
        draw.rectangle(
            (
                box["x"],
                box["y"],
                box["x"] + box["width"] - 1,
                box["y"] + box["height"] - 1,
            ),
            outline=(255, 64, 64, 255),
            width=2,
        )
    preview_path = out_dir / f"{spec['name']}-atlas-preview.png"
    preview.save(preview_path)

    manifest = {
        "type": "pyramid-face-z-aware-continuity-atlas",
        "face": spec["name"],
        "version": version,
        "tileSize": TILE_SIZE,
        "atlasSize": {"width": atlas.width, "height": atlas.height},
        "expectedUpscale2x": {"width": atlas.width * 2, "height": atlas.height * 2},
        "finalScale": 2,
        "primaryClientId": spec["primary"],
        "extensionClientId": spec["extension"],
        "pairOffset": list(spec["pairOffset"]),
        "levelRecession": list(spec["levelRecession"]),
        "tangent": list(spec["tangent"]),
        "levels": levels,
        "repeat": repeat,
        "targetSpriteIds": sorted(
            {
                int(sprite_id)
                for client_id in client_ids
                for sprite_id in things[client_id].unique_sprites
            }
        ),
        "targetPlacements": targets,
        "panels": panel_rows,
        "input": str(input_path),
        "preview": str(preview_path),
        "notes": [
            "Map positions are projected with the OTClient floor shift before rendering.",
            "Each higher level uses the face-specific recession vector toward the pyramid center.",
            "The selected target is redrawn last and must be recut with original sprite alpha.",
        ],
    }
    manifest_path = out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (out_dir / "_LEIA-ME.txt").write_text(
        (
            f"UPSCALE ONLY {input_path.name}\n"
            f"Expected 2x output size: {atlas.width * 2}x{atlas.height * 2}.\n"
            "Do not upscale the preview image.\n"
        ),
        encoding="ascii",
    )
    return {
        "face": spec["name"],
        "input": str(input_path),
        "preview": str(preview_path),
        "manifest": str(manifest_path),
        "expectedUpscale2x": [atlas.width * 2, atlas.height * 2],
        "targetSpriteIds": manifest["targetSpriteIds"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build z-aware continuity atlases for the remaining pyramid faces."
    )
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--spr", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, required=True)
    parser.add_argument("--version", type=int, default=772)
    parser.add_argument("--levels", type=int, default=9)
    parser.add_argument("--repeat", type=int, default=24)
    args = parser.parse_args()

    if args.out_root.exists():
        shutil.rmtree(args.out_root)
    args.out_root.mkdir(parents=True)

    client_ids = {
        int(spec[key])
        for spec in FACE_SPECS
        for key in ("primary", "extension")
    }
    things = parse_dat(args.dat, args.version, THING_CATEGORY_ITEM, client_ids)
    images = {client_id: render_item(things[client_id], args.spr) for client_id in client_ids}
    summary = [
        build_face(
            spec,
            things,
            images,
            args.spr,
            args.version,
            args.out_root,
            args.levels,
            args.repeat,
        )
        for spec in FACE_SPECS
    ]
    summary_path = args.out_root / "_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({"outRoot": str(args.out_root), "faces": summary}, indent=2))


if __name__ == "__main__":
    main()
