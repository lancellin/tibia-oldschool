#!/usr/bin/env python3
import argparse
import json
import re
import shutil
from pathlib import Path

from PIL import Image

from build_cwm import build_cwm


def load_manifest(path: Path) -> dict[int, dict[str, object]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError(f"Expected a list in {path}")
    return {int(row["clientId"]): row for row in payload}


def client_id_from_filename(path: Path) -> int | None:
    match = re.match(r"item-(\d+)-mosaic", path.name)
    if not match:
        return None
    return int(match.group(1))


def choose_latest_results(results_root: Path, manifest: dict[int, dict[str, object]], recursive: bool) -> dict[int, Path]:
    pattern = "**/*.png" if recursive else "*.png"
    selected: dict[int, Path] = {}
    for path in sorted(results_root.glob(pattern)):
        client_id = client_id_from_filename(path)
        if client_id is None or client_id not in manifest:
            continue
        if client_id not in selected or path.stat().st_mtime > selected[client_id].stat().st_mtime:
            selected[client_id] = path
    return selected


def apply_original_alpha(tile: Image.Image, original_folder: Path, sprite_id: int, size: tuple[int, int]) -> Image.Image:
    original_path = original_folder / f"{sprite_id}.png"
    result = tile.convert("RGBA")
    if not original_path.exists():
        return result

    with Image.open(original_path) as original:
        alpha = original.convert("RGBA").getchannel("A").resize(size, Image.Resampling.LANCZOS)
    result.putalpha(alpha)
    return result


def centered_occurrences(pattern_sprites: list[int], pattern_w: int, pattern_h: int) -> dict[int, tuple[int, int]]:
    chosen: dict[int, tuple[float, int, int]] = {}
    center_x = (pattern_w - 1) / 2
    center_y = (pattern_h - 1) / 2

    for py in range(pattern_h):
        for px in range(pattern_w):
            sprite_id = pattern_sprites[py * pattern_w + px]
            if sprite_id <= 0:
                continue
            distance = (px - center_x) ** 2 + (py - center_y) ** 2
            if sprite_id not in chosen or distance < chosen[sprite_id][0]:
                chosen[sprite_id] = (distance, px, py)

    return {sprite_id: (px, py) for sprite_id, (_distance, px, py) in chosen.items()}


def extract_ground(row: dict[str, object], source: Path, downscaled_dir: Path, sprites_dir: Path) -> dict[str, object]:
    client_id = int(row["clientId"])
    grid_w = int(row["gridWidth"])
    grid_h = int(row["gridHeight"])
    pattern_w = int(row["patternX"])
    pattern_h = int(row["patternY"])
    source_tile_w = int(row["tileWidth"])
    source_tile_h = int(row["tileHeight"])

    with Image.open(source) as image:
        upscaled = image.convert("RGBA")

    expected_w = grid_w * source_tile_w * 4
    expected_h = grid_h * source_tile_h * 4
    if upscaled.size != (expected_w, expected_h):
        raise ValueError(f"{source} expected {expected_w}x{expected_h}, got {upscaled.size}")

    final_tile_w = source_tile_w * 2
    final_tile_h = source_tile_h * 2
    downscaled = upscaled.resize((upscaled.width // 2, upscaled.height // 2), Image.Resampling.LANCZOS)
    downscaled_path = downscaled_dir / f"item-{client_id}-mosaic.png"
    downscaled.save(downscaled_path)

    start_x = (grid_w - pattern_w) // 2
    start_y = (grid_h - pattern_h) // 2
    pattern_sprites = [int(sprite_id) for sprite_id in row["patternSprites"]]
    choices = centered_occurrences(pattern_sprites, pattern_w, pattern_h)
    original_folder = Path(str(row["folder"]))

    extracted: list[int] = []
    for sprite_id, (px, py) in sorted(choices.items()):
        left = (start_x + px) * final_tile_w
        top = (start_y + py) * final_tile_h
        tile = downscaled.crop((left, top, left + final_tile_w, top + final_tile_h))
        tile = apply_original_alpha(tile, original_folder, sprite_id, (final_tile_w, final_tile_h))
        tile.save(sprites_dir / f"{sprite_id}.png")
        extracted.append(sprite_id)

    return {
        "clientId": client_id,
        "name": row.get("name", ""),
        "source": str(source),
        "downscaled": str(downscaled_path),
        "sprites": extracted,
        "pattern": f"{pattern_w}x{pattern_h}",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a 2x ground CWM from 4x Upscayl mosaic outputs.")
    parser.add_argument("--results-root", required=True, help="Folder containing 4x Upscayl output PNGs")
    parser.add_argument("--manifest", required=True, help="Ground mosaic manifest JSON")
    parser.add_argument("--out-root", required=True, help="Output root for downscaled mosaics, sprites, summary and CWM")
    parser.add_argument("--cwm-name", default="Tibia.grounds-gmic-4x-to-2x.cwm", help="Output CWM filename")
    parser.add_argument("--sprites-count", type=int, default=10962, help="CWM sprite count header")
    parser.add_argument("--recursive", action="store_true", help="Find PNG outputs recursively under --results-root")
    args = parser.parse_args()

    results_root = Path(args.results_root)
    manifest = load_manifest(Path(args.manifest))
    out_root = Path(args.out_root)
    downscaled_dir = out_root / "processed-2x"
    sprites_dir = out_root / "sprites"
    built_dir = out_root / "built"

    if out_root.exists():
        shutil.rmtree(out_root)
    downscaled_dir.mkdir(parents=True, exist_ok=True)
    sprites_dir.mkdir(parents=True, exist_ok=True)
    built_dir.mkdir(parents=True, exist_ok=True)

    selected = choose_latest_results(results_root, manifest, args.recursive)
    if not selected:
        raise SystemExit(f"No matching Upscayl output PNGs found in {results_root}")

    summary: list[dict[str, object]] = []
    for client_id in sorted(selected):
        summary.append(extract_ground(manifest[client_id], selected[client_id], downscaled_dir, sprites_dir))

    cwm_path = built_dir / args.cwm_name
    build_cwm(sprites_dir, cwm_path, args.sprites_count)

    (out_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({
        "cwm": str(cwm_path),
        "itemCount": len(summary),
        "spriteCount": len(list(sprites_dir.glob("*.png"))),
        "summary": str(out_root / "summary.json"),
    }, indent=2))


if __name__ == "__main__":
    main()
