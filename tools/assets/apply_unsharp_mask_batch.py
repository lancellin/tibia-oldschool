#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image, ImageFilter


def apply_unsharp_mask(image: Image.Image, radius: float, amount: float, threshold: int) -> Image.Image:
    rgba = image.convert("RGBA")
    rgb = Image.new("RGB", rgba.size, (0, 0, 0))
    rgb.paste(rgba.convert("RGB"))
    alpha = rgba.getchannel("A")

    sharpened = rgb.filter(
        ImageFilter.UnsharpMask(
            radius=radius,
            percent=int(round(amount * 100)),
            threshold=threshold,
        )
    )
    result = sharpened.convert("RGBA")
    result.putalpha(alpha)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Apply an unsharp mask to a folder of PNG files.")
    parser.add_argument("--input", required=True, help="Input folder containing PNG files")
    parser.add_argument("--output", required=True, help="Output folder for processed PNG files")
    parser.add_argument("--radius", type=float, default=2.8, help="Blur radius, matching GIMP's radius value")
    parser.add_argument("--amount", type=float, default=0.3, help="Sharpen amount, matching GIMP's amount value")
    parser.add_argument("--threshold", type=int, default=0, help="Threshold, matching GIMP's threshold value")
    parser.add_argument("--recursive", action="store_true", help="Process PNGs recursively")
    args = parser.parse_args()

    input_dir = Path(args.input)
    output_dir = Path(args.output)
    if not input_dir.exists():
        raise FileNotFoundError(f"Input folder was not found: {input_dir}")

    output_dir.mkdir(parents=True, exist_ok=True)
    pattern = "**/*.png" if args.recursive else "*.png"
    files = sorted(input_dir.glob(pattern))
    if not files:
        raise SystemExit(f"No PNG files found in {input_dir}")

    for source in files:
        relative = source.relative_to(input_dir)
        target = output_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(source) as image:
            result = apply_unsharp_mask(image, args.radius, args.amount, args.threshold)
        result.save(target)
        print(f"[ok] {source} -> {target}")

    print(f"[done] processed {len(files)} PNG files")


if __name__ == "__main__":
    main()
