#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image


def resize_file(src: Path, dst: Path, scale: float, suffix: str | None) -> Path:
    image = Image.open(src).convert("RGBA")
    out_w = max(1, round(image.width * scale))
    out_h = max(1, round(image.height * scale))
    resized = image.resize((out_w, out_h), Image.Resampling.LANCZOS)

    if dst.is_dir() or (not dst.suffix and not dst.exists()):
        dst.mkdir(parents=True, exist_ok=True)
        target = dst / f"{src.stem}{suffix or ''}.png"
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        target = dst

    resized.save(target)
    print(f"[ok] {src} -> {target} ({image.width}x{image.height} -> {out_w}x{out_h})")
    return target


def iter_input_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return sorted(
        p for p in path.iterdir()
        if p.is_file() and p.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp"}
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Resize one image or a folder of images using Lanczos resampling.")
    parser.add_argument("--input", required=True, help="Input file or folder")
    parser.add_argument("--output", required=True, help="Output file or folder")
    parser.add_argument("--scale", type=float, required=True, help="Scale factor, e.g. 0.5 for half size")
    parser.add_argument("--suffix", default="-resized", help="Suffix to add when output is a directory")
    args = parser.parse_args()

    src = Path(args.input)
    dst = Path(args.output)
    files = iter_input_files(src)
    if not files:
        raise SystemExit(f"No input images found in {src}")

    suffix = None if dst.is_file() else args.suffix
    for file in files:
        resize_file(file, dst, args.scale, suffix)


if __name__ == "__main__":
    main()
