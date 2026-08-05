#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def collect_sprite_pngs(input_dir: Path) -> list[tuple[int, Path]]:
    sprites: list[tuple[int, Path]] = []
    for path in sorted(input_dir.glob("*.png")):
        try:
            sprite_id = int(path.stem)
        except ValueError:
            continue
        if sprite_id <= 0:
            continue
        sprites.append((sprite_id, path))
    return sprites


def build_cwm(input_dir: Path, output_file: Path, sprites_count: int | None) -> None:
    sprites = collect_sprite_pngs(input_dir)
    if not sprites:
        raise SystemExit(f"No numeric PNG files found in {input_dir}")

    final_sprites_count = sprites_count if sprites_count is not None else max(sprite_id for sprite_id, _ in sprites)
    if final_sprites_count < 0 or final_sprites_count > 0xFFFF:
        raise SystemExit("sprites_count must fit in an unsigned 16-bit integer")

    payloads: list[tuple[int, bytes]] = []
    for sprite_id, path in sprites:
        payloads.append((sprite_id, path.read_bytes()))

    output_file.parent.mkdir(parents=True, exist_ok=True)
    with output_file.open("wb") as fp:
        fp.write(struct.pack("<BHI", 0x01, final_sprites_count, len(payloads)))

        offset = 0
        for sprite_id, payload in payloads:
            sprite_name = str(sprite_id).encode("utf-8")
            fp.write(struct.pack("<IIH", offset, len(payload), len(sprite_name)))
            fp.write(sprite_name)
            offset += len(payload)

        for _, payload in payloads:
            fp.write(payload)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a partial Tibia.cwm override from numeric PNG files.")
    parser.add_argument("--input", required=True, help="Directory containing PNG files named like 724.png")
    parser.add_argument("--output", required=True, help="Output path for Tibia.cwm")
    parser.add_argument(
        "--sprites-count",
        type=int,
        help="Optional CWM sprite count header. Defaults to the highest sprite id in the input folder.",
    )
    args = parser.parse_args()

    build_cwm(Path(args.input), Path(args.output), args.sprites_count)
    print(f"[ok] wrote {args.output}")


if __name__ == "__main__":
    main()
