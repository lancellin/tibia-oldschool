#!/usr/bin/env python3
import argparse
import hashlib
import json
import struct
from io import BytesIO
from pathlib import Path

from PIL import Image

from merge_cwm import read_cwm


MAGIC = b"HDP1"


def build_hdp(input_cwm: Path, output_hdp: Path, summary_path: Path | None = None) -> dict[str, object]:
    version, sprites_count, sprites = read_cwm(input_cwm)
    if version != 1:
        raise ValueError(f"Unsupported CWM version: {version}")

    entries: list[tuple[int, int, int, int, int, int]] = []
    unique_payloads: list[bytes] = []
    payload_offsets: dict[bytes, int] = {}
    payload_bytes = 0

    for sprite_id, png_payload in sorted(sprites.items()):
        with Image.open(BytesIO(png_payload)) as image:
            rgba = image.convert("RGBA")
            width, height = rgba.size
            raw_payload = rgba.tobytes()

        expected_size = width * height * 4
        if len(raw_payload) != expected_size:
            raise ValueError(f"Unexpected RGBA size for sprite {sprite_id}: {len(raw_payload)} != {expected_size}")

        digest = hashlib.sha256(raw_payload).digest()
        if digest not in payload_offsets:
            payload_offsets[digest] = payload_bytes
            unique_payloads.append(raw_payload)
            payload_bytes += len(raw_payload)

        flags = 1 if raw_payload[3::4].count(0) > 0 else 0
        entries.append((sprite_id, payload_offsets[digest], width, height, len(raw_payload), flags))

    output_hdp.parent.mkdir(parents=True, exist_ok=True)
    with output_hdp.open("wb") as output:
        output.write(MAGIC)
        output.write(struct.pack("<HI", sprites_count, len(entries)))

        for sprite_id, offset, width, height, size, flags in entries:
            output.write(struct.pack("<IIHHIB", sprite_id, offset, width, height, size, flags))

        for payload in unique_payloads:
            output.write(payload)

    summary = {
        "inputCwm": str(input_cwm),
        "outputHdp": str(output_hdp),
        "spritesCount": sprites_count,
        "entries": len(entries),
        "uniquePayloads": len(unique_payloads),
        "duplicateEntries": len(entries) - len(unique_payloads),
        "payloadBytes": payload_bytes,
        "outputBytes": output_hdp.stat().st_size,
        "sha256": hashlib.sha256(output_hdp.read_bytes()).hexdigest().upper(),
    }

    if summary_path:
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Build an experimental raw RGBA HDP pack from a Tibia CWM.")
    parser.add_argument("--input-cwm", type=Path, required=True)
    parser.add_argument("--output-hdp", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    print(json.dumps(build_hdp(args.input_cwm, args.output_hdp, args.summary), indent=2))


if __name__ == "__main__":
    main()
