#!/usr/bin/env python3
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image


DEFAULT_SOURCE = Path(r"D:\AI\ComfyUI\input\tibia-oldschool-ankrahmun-pyramids\01-primary-pyramid-ramps")
DEFAULT_OUTPUT = Path(r"D:\AI\ComfyUI\input\tibia-oldschool-ankrahmun-pyramids\08-z-aware-lossless-atlases-1x")
TILE_SIZE = 32


FACE_RECIPES = [
    {
        "name": "south-face-1966-zaware-over-1967-repeat-x",
        "orientation": "z_aware_pair_repeat_x",
        "top": 1966,
        "bottom": 1967,
        "top_delta": [1, 1, -1],
        "step": [1, 0, 0],
        "note": "South face doodad pair: top ramp over bottom ramp.",
    },
    {
        "name": "north-face-1964-zaware-over-1965-repeat-x",
        "orientation": "z_aware_pair_repeat_x",
        "top": 1964,
        "bottom": 1965,
        "top_delta": [1, 1, -1],
        "step": [1, 0, 0],
        "note": "North face doodad pair: top ramp over bottom ramp.",
    },
    {
        "name": "east-face-1962-zaware-over-1963-repeat-y",
        "orientation": "z_aware_pair_repeat_y",
        "top": 1962,
        "bottom": 1963,
        "top_delta": [1, 1, -1],
        "step": [0, 1, 0],
        "note": "East face pair, first pass: high/top part beside low/bottom part.",
    },
    {
        "name": "west-face-1960-zaware-over-1961-repeat-y",
        "orientation": "z_aware_pair_repeat_y",
        "top": 1960,
        "bottom": 1961,
        "top_delta": [1, 1, -1],
        "step": [0, 1, 0],
        "note": "West face pair, first pass: low/bottom part beside high/top part.",
    },
]


CORNER_RECIPES = [
    {
        "name": "southwest-corner-2194-over-2193",
        "top": 2194,
        "bottom": 2193,
        "note": "Southwest corner top and bottom pieces.",
    },
    {
        "name": "northwest-corner-2196-over-2195",
        "top": 2196,
        "bottom": 2195,
        "note": "Northwest corner top and bottom pieces.",
    },
    {
        "name": "northeast-corner-2198-over-2197",
        "top": 2198,
        "bottom": 2197,
        "note": "Northeast corner with continuation/bottom piece.",
    },
    {
        "name": "southeast-corner-2192-over-2191",
        "top": 2192,
        "bottom": 2191,
        "note": "Southeast corner with continuation/bottom piece.",
    },
]


def load_item(source_root: Path, client_id: int) -> Image.Image:
    path = source_root / f"item-{client_id}" / f"item-{client_id}-object-context.png"
    if not path.exists():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def paste_with_alpha(canvas: Image.Image, image: Image.Image, xy: tuple[int, int]) -> None:
    canvas.alpha_composite(image, dest=xy)


def project_position(x: int, y: int, z: int, relative_z: int = 0) -> tuple[int, int]:
    floor_delta = relative_z - z
    return ((x - floor_delta) * TILE_SIZE, (y - floor_delta) * TILE_SIZE)


def build_projected_scene(source_root: Path, output: Path, placements: list[dict[str, int]]) -> dict[str, object]:
    loaded: dict[int, Image.Image] = {}
    projected: list[dict[str, object]] = []

    for placement in placements:
        client_id = int(placement["clientId"])
        image = loaded.setdefault(client_id, load_item(source_root, client_id))
        x, y = project_position(int(placement["x"]), int(placement["y"]), int(placement["z"]))
        projected.append({"placement": placement, "image": image, "screenX": x, "screenY": y})

    min_x = min(int(item["screenX"]) for item in projected)
    min_y = min(int(item["screenY"]) for item in projected)
    max_x = max(int(item["screenX"]) + item["image"].width for item in projected)
    max_y = max(int(item["screenY"]) + item["image"].height for item in projected)

    pad = TILE_SIZE
    canvas = Image.new("RGBA", (max_x - min_x + pad * 2, max_y - min_y + pad * 2), (0, 0, 0, 0))
    manifest_placements = []

    # Draw lower floors first, then upper floors, matching the client order.
    for item in sorted(projected, key=lambda entry: int(entry["placement"]["z"]), reverse=True):
        x = int(item["screenX"]) - min_x + pad
        y = int(item["screenY"]) - min_y + pad
        paste_with_alpha(canvas, item["image"], (x, y))
        manifest_placements.append({
            "clientId": int(item["placement"]["clientId"]),
            "map": {"x": int(item["placement"]["x"]), "y": int(item["placement"]["y"]), "z": int(item["placement"]["z"])},
            "screen": {"x": x, "y": y},
        })

    canvas.save(output)
    return {"file": str(output), "size": canvas.size, "placements": manifest_placements}


def build_z_aware_pair_repeat(source_root: Path, output: Path, top_id: int, bottom_id: int, top_delta: list[int], step: list[int], repeat: int) -> dict[str, object]:
    placements: list[dict[str, int]] = []
    for index in range(repeat):
        base_x = index * int(step[0])
        base_y = index * int(step[1])
        base_z = index * int(step[2])
        placements.append({"clientId": bottom_id, "x": base_x, "y": base_y, "z": base_z})
        placements.append({
            "clientId": top_id,
            "x": base_x + int(top_delta[0]),
            "y": base_y + int(top_delta[1]),
            "z": base_z + int(top_delta[2]),
        })
    return build_projected_scene(source_root, output, placements)


def build_z_aware_corner(source_root: Path, output: Path, top_id: int, bottom_id: int, top_delta: list[int]) -> dict[str, object]:
    return build_projected_scene(
        source_root,
        output,
        [
            {"clientId": bottom_id, "x": 0, "y": 0, "z": 0},
            {"clientId": top_id, "x": int(top_delta[0]), "y": int(top_delta[1]), "z": int(top_delta[2])},
        ],
    )


def build_vertical_stack_repeat_x(source_root: Path, output: Path, top_id: int, bottom_id: int, repeat: int) -> dict[str, object]:
    top = load_item(source_root, top_id)
    bottom = load_item(source_root, bottom_id)

    tile_w = max(top.width, bottom.width)
    tile_h = max(top.height, bottom.height)
    canvas = Image.new("RGBA", (tile_w * repeat, tile_h * 2), (0, 0, 0, 0))
    placements = []
    for index in range(repeat):
        x = index * tile_w
        paste_with_alpha(canvas, top, (x, 0))
        paste_with_alpha(canvas, bottom, (x, tile_h))
        placements.append({"clientId": top_id, "x": x, "y": 0})
        placements.append({"clientId": bottom_id, "x": x, "y": tile_h})

    canvas.save(output)
    return {"file": str(output), "size": canvas.size, "placements": placements}


def build_horizontal_stack_repeat_y(source_root: Path, output: Path, left_id: int, right_id: int, repeat: int) -> dict[str, object]:
    left = load_item(source_root, left_id)
    right = load_item(source_root, right_id)

    tile_w = max(left.width, right.width)
    tile_h = max(left.height, right.height)
    canvas = Image.new("RGBA", (tile_w * 2, tile_h * repeat), (0, 0, 0, 0))
    placements = []
    for index in range(repeat):
        y = index * tile_h
        paste_with_alpha(canvas, left, (0, y))
        paste_with_alpha(canvas, right, (tile_w, y))
        placements.append({"clientId": left_id, "x": 0, "y": y})
        placements.append({"clientId": right_id, "x": tile_w, "y": y})

    canvas.save(output)
    return {"file": str(output), "size": canvas.size, "placements": placements}


def build_corner_variants(source_root: Path, output_root: Path, recipe: dict[str, object]) -> list[dict[str, object]]:
    top_id = int(recipe["top"])
    bottom_id = int(recipe["bottom"])
    top = load_item(source_root, top_id)
    bottom = load_item(source_root, bottom_id)

    tile_w = max(top.width, bottom.width)
    tile_h = max(top.height, bottom.height)
    variants = []

    vertical = Image.new("RGBA", (tile_w, tile_h * 2), (0, 0, 0, 0))
    paste_with_alpha(vertical, top, (0, 0))
    paste_with_alpha(vertical, bottom, (0, tile_h))
    vertical_path = output_root / f"{recipe['name']}-vertical.png"
    vertical.save(vertical_path)
    variants.append({
        "file": str(vertical_path),
        "size": vertical.size,
        "placements": [
            {"clientId": top_id, "x": 0, "y": 0},
            {"clientId": bottom_id, "x": 0, "y": tile_h},
        ],
    })

    horizontal = Image.new("RGBA", (tile_w * 2, tile_h), (0, 0, 0, 0))
    paste_with_alpha(horizontal, top, (0, 0))
    paste_with_alpha(horizontal, bottom, (tile_w, 0))
    horizontal_path = output_root / f"{recipe['name']}-horizontal.png"
    horizontal.save(horizontal_path)
    variants.append({
        "file": str(horizontal_path),
        "size": horizontal.size,
        "placements": [
            {"clientId": top_id, "x": 0, "y": 0},
            {"clientId": bottom_id, "x": tile_w, "y": 0},
        ],
    })

    return variants


def main() -> None:
    parser = argparse.ArgumentParser(description="Build lossless manual atlases for Ankrahmun pyramid ramp tests.")
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--repeat", type=int, default=6)
    parser.add_argument("--keep-existing", action="store_true")
    args = parser.parse_args()

    if args.repeat < 2:
        raise SystemExit("--repeat must be at least 2")

    if args.out_root.exists() and not args.keep_existing:
        shutil.rmtree(args.out_root)
    args.out_root.mkdir(parents=True, exist_ok=True)

    manifest: list[dict[str, object]] = []

    for recipe in FACE_RECIPES:
        output = args.out_root / f"{recipe['name']}.png"
        if recipe["orientation"] in ("z_aware_pair_repeat_x", "z_aware_pair_repeat_y"):
            built = build_z_aware_pair_repeat(
                args.source_root,
                output,
                int(recipe["top"]),
                int(recipe["bottom"]),
                list(recipe["top_delta"]),
                list(recipe["step"]),
                args.repeat,
            )
        else:
            raise ValueError(f"Unknown orientation: {recipe['orientation']}")
        built.update({"name": recipe["name"], "orientation": recipe["orientation"], "note": recipe["note"]})
        manifest.append(built)

    for recipe in CORNER_RECIPES:
        output = args.out_root / f"{recipe['name']}-zaware.png"
        built = build_z_aware_corner(args.source_root, output, int(recipe["top"]), int(recipe["bottom"]), [1, 1, -1])
        built.update({"name": recipe["name"], "orientation": "z_aware_corner", "note": recipe["note"]})
        manifest.append(built)

    manifest_path = args.out_root / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"outRoot": str(args.out_root), "fileCount": len(manifest), "manifest": str(manifest_path)}, indent=2))


if __name__ == "__main__":
    main()
