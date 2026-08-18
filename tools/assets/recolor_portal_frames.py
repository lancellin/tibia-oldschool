#!/usr/bin/env python3
"""Recolors the extracted magic forcefield (client 1949) frames.

Produces hue-shifted variants (red and gold) of the original blue frames,
preserving saturation, value and alpha exactly - only the hue changes, so
the artwork stays pixel-identical in structure.
"""
import argparse
import shutil
from pathlib import Path

from PIL import Image

SOURCE_DIR = Path(__file__).resolve().parent / "old_portal_1949" / "item-1949"
FRAME_IDS = (197, 198, 199)

# PIL HSV hue channel is 0-255 for 0-360 degrees.
TARGETS = {
    "red": 0,        # 0 degrees
    "gold": 32,      # ~45 degrees
}


def recolor(image: Image.Image, hue: int) -> Image.Image:
    rgba = image.convert("RGBA")
    r, g, b, a = rgba.split()
    h, s, v = Image.merge("RGB", (r, g, b)).convert("HSV").split()
    h = h.point(lambda _value: hue)
    rgb = Image.merge("HSV", (h, s, v)).convert("RGB")
    rgb.putalpha(a)
    return rgb


def build_sheet(frames: list, out_path: Path) -> None:
    width = 32 * len(frames)
    sheet = Image.new("RGBA", (width, 32), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, (32 * index, 0), frame)
    sheet.save(out_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Recolor forcefield frames (red/gold).")
    parser.add_argument("--copy-to", type=Path, help="Optional directory to copy the results to.")
    args = parser.parse_args()

    out_root = Path(__file__).resolve().parent / "portal_recolors"
    for name, hue in TARGETS.items():
        out_dir = out_root / name
        out_dir.mkdir(parents=True, exist_ok=True)
        frames = []
        for frame_id in FRAME_IDS:
            source = Image.open(SOURCE_DIR / f"{frame_id}.png")
            tinted = recolor(source, hue)
            tinted.save(out_dir / f"{frame_id}.png")
            frames.append(tinted)
        build_sheet(frames, out_dir / "sheet.png")
        print(f"[ok] {out_dir}")

    if args.copy_to:
        for name in TARGETS:
            target = args.copy_to / f"Portal Forcefield {name}"
            shutil.copytree(out_root / name, target, dirs_exist_ok=True)
            print(f"[ok] copied to {target}")


if __name__ == "__main__":
    main()
