#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw


SCALE = 6
SPRITE_SIZE = 32
SCALED_SIZE = SPRITE_SIZE * SCALE


def nearest(image: Image.Image) -> Image.Image:
    return image.resize((SCALED_SIZE, SCALED_SIZE), Image.Resampling.NEAREST)


def add_header(image: Image.Image, title: str, subtitle: str = "") -> Image.Image:
    header_height = 42
    output = Image.new("RGBA", (image.width, image.height + header_height), (24, 24, 24, 255))
    output.alpha_composite(image, (0, header_height))
    draw = ImageDraw.Draw(output)
    draw.text((8, 7), title, fill=(255, 255, 255, 255))
    if subtitle:
        draw.text((8, 23), subtitle, fill=(190, 190, 190, 255))
    return output


def static_comparison(
    source_dir: Path,
    rebased_dir: Path,
    metadata: dict[str, object],
    output: Path,
) -> None:
    source_sprite_ids = metadata["sourceSpriteIds"]
    rows: list[tuple[int, int, Path, Path]] = []

    for overlay_index, sprite_id in enumerate(source_sprite_ids):
        before = next(
            source_dir.glob(f"ordered-frames/frame-{overlay_index:03d}-{sprite_id}.png")
        )
        after = rebased_dir.parent / "review-after-original-order" / f"after-{overlay_index:03d}-sprite-{sprite_id}.png"
        rows.append((overlay_index, sprite_id, before, after))

    label_width = 116
    gap = 12
    row_height = SCALED_SIZE + 24
    width = label_width + SCALED_SIZE * 2 + gap + 18
    height = 52 + len(rows) * row_height
    sheet = Image.new("RGBA", (width, height), (28, 28, 28, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((label_width + 55, 10), "ANTES", fill=(255, 215, 120, 255))
    draw.text((label_width + SCALED_SIZE + gap + 53, 10), "DEPOIS", fill=(120, 225, 255, 255))
    draw.text((8, 30), "Azul antigo", fill=(180, 180, 180, 255))
    draw.text((label_width + SCALED_SIZE + gap, 30), "Agua 7.4 recomposta", fill=(180, 180, 180, 255))

    for row, (index, sprite_id, before_path, after_path) in enumerate(rows):
        y = 52 + row * row_height
        before = nearest(Image.open(before_path).convert("RGBA"))
        after = nearest(Image.open(after_path).convert("RGBA"))
        draw.text((8, y + 74), f"ref {index:02d}\nsprite {sprite_id}", fill=(235, 235, 235, 255))
        sheet.alpha_composite(before, (label_width, y))
        sheet.alpha_composite(after, (label_width + SCALED_SIZE + gap, y))
        draw.rectangle(
            (label_width - 1, y - 1, label_width + SCALED_SIZE, y + SCALED_SIZE),
            outline=(90, 90, 90, 255),
        )
        x2 = label_width + SCALED_SIZE + gap
        draw.rectangle((x2 - 1, y - 1, x2 + SCALED_SIZE, y + SCALED_SIZE), outline=(90, 90, 90, 255))

    sheet.save(output)


def animation_comparison(
    source_dir: Path,
    rebased_dir: Path,
    metadata: dict[str, object],
    output: Path,
) -> None:
    source_layout = metadata["sourceLayout"]
    source_sprite_ids = metadata["sourceSpriteIds"]
    refs = metadata["outputReferences"]
    source_patterns = source_layout["patternX"] * source_layout["patternY"]
    frames: list[Image.Image] = []

    for phase in range(16):
        old_phase = min(source_layout["frames"] - 1, phase * source_layout["frames"] // 16)
        overlay_index = old_phase * source_patterns
        sprite_id = source_sprite_ids[overlay_index]
        before_path = next(
            source_dir.glob(f"ordered-frames/frame-{overlay_index:03d}-{sprite_id}.png")
        )
        match = next(
            ref
            for ref in refs
            if ref["phase"] == phase and ref["patternX"] == 0 and ref["patternY"] == 0
        )
        after_path = next(
            rebased_dir.glob(
                f"frame-{match['index']:03d}_phase-{phase:02d}_px-0_py-0"
                f"_old-{match['sourceOverlayIndex']:02d}.png"
            )
        )
        before = nearest(Image.open(before_path).convert("RGBA"))
        after = nearest(Image.open(after_path).convert("RGBA"))
        canvas = Image.new("RGBA", (SCALED_SIZE * 2 + 24, SCALED_SIZE), (24, 24, 24, 255))
        canvas.alpha_composite(before, (0, 0))
        canvas.alpha_composite(after, (SCALED_SIZE + 24, 0))
        canvas = add_header(canvas, "ANTES                              DEPOIS", f"fase nova {phase:02d}")
        frames.append(canvas.convert("P", palette=Image.Palette.ADAPTIVE))

    frames[0].save(
        output,
        save_all=True,
        append_images=frames[1:],
        duration=180,
        loop=0,
        disposal=2,
    )


def overview_comparison(
    source_root: Path,
    rebased_root: Path,
    manifest: list[dict[str, object]],
    output: Path,
) -> None:
    thumb_scale = 3
    thumb_size = SPRITE_SIZE * thumb_scale
    row_height = thumb_size + 12
    label_width = 150
    gap = 12
    width = label_width + thumb_size * 2 + gap + 18
    height = 42 + len(manifest) * row_height
    sheet = Image.new("RGBA", (width, height), (26, 26, 26, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((label_width + 30, 13), "ANTES", fill=(255, 215, 120, 255))
    draw.text((label_width + thumb_size + gap + 28, 13), "DEPOIS", fill=(120, 225, 255, 255))

    for row, metadata in enumerate(manifest):
        server_id = metadata["serverId"]
        client_id = metadata["clientId"]
        source_dir = source_root / f"server-{server_id}_client-{client_id}" / "ordered-frames"
        rebased_dir = rebased_root / f"server-{server_id}_client-{client_id}" / "rebased-4x2x16"
        sprite_id = metadata["sourceSpriteIds"][0]
        before_path = next(source_dir.glob(f"frame-000-{sprite_id}.png"))
        after_path = next(rebased_dir.glob("frame-000_phase-00_px-0_py-0_old-00.png"))
        before = Image.open(before_path).convert("RGBA").resize((thumb_size, thumb_size), Image.Resampling.NEAREST)
        after = Image.open(after_path).convert("RGBA").resize((thumb_size, thumb_size), Image.Resampling.NEAREST)
        y = 42 + row * row_height
        draw.text((8, y + 35), f"server {server_id}\nclient {client_id}", fill=(235, 235, 235, 255))
        sheet.alpha_composite(before, (label_width, y))
        sheet.alpha_composite(after, (label_width + thumb_size + gap, y))

    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build simple before/after reviews for rebased water borders.")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--rebased-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads((args.rebased_root / "manifest.json").read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    individual = args.output / "individual"
    individual.mkdir(parents=True, exist_ok=True)

    for metadata in manifest:
        server_id = metadata["serverId"]
        client_id = metadata["clientId"]
        name = f"server-{server_id}_client-{client_id}"
        source_dir = args.source_root / name
        rebased_dir = args.rebased_root / name / "rebased-4x2x16"
        static_comparison(
            source_dir,
            rebased_dir,
            metadata,
            individual / f"{name}_ANTES-DEPOIS.png",
        )
        animation_comparison(
            source_dir,
            rebased_dir,
            metadata,
            individual / f"{name}_ANTES-DEPOIS.gif",
        )
        print(f"[ok] {name}")

    overview_comparison(
        args.source_root,
        args.rebased_root,
        manifest,
        args.output / "TODAS-AS-BORDAS_ANTES-DEPOIS.png",
    )
    print(f"[ok] wrote {args.output}")


if __name__ == "__main__":
    main()
