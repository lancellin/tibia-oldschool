#!/usr/bin/env python3
import argparse
import csv
import json
import struct
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

from PIL import Image

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import (
    THING_ATTR_GROUND,
    THING_CATEGORY_CREATURE,
    THING_CATEGORY_EFFECT,
    THING_CATEGORY_ITEM,
    THING_CATEGORY_MISSILE,
    category_name,
    parse_dat,
)
from merge_cwm import read_cwm


DEFAULT_DAT = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")
DEFAULT_SPR = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")
DEFAULT_CWM = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.cwm")
DEFAULT_OTB = Path(r"D:\tibia-oldschool\server\data\items\items.otb")
DEFAULT_RME = Path(r"D:\tibia-oldschool\sources\rme-otacademy\data\772")
DEFAULT_OUTPUT = Path(r"C:\Users\guisu\OneDrive\Área de Trabalho\Sprites simples")


STATUS_CANDIDATE = "para_upscale"
STATUS_ALREADY_HD = "ja_hd"
STATUS_CONTINUITY = "continuidade"
STATUS_MULTI_TILE = "maior_que_1_tile"
STATUS_LAYERED = "revisao_camadas"

MANUAL_CONTINUITY_SPRITE_RANGES = (
    (6842, 6975),
    (7504, 7507),
    (7525, 7528),
    (7624, 7663),
    (8461, 8675),
    (9615, 9626),
    (9642, 9655),
)
MANUAL_CONTINUITY_SPRITE_IDS = {
    sprite_id
    for start, end in MANUAL_CONTINUITY_SPRITE_RANGES
    for sprite_id in range(start, end + 1)
}


class SprArchive:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        self.signature = self.data[:4].hex()
        self.count = struct.unpack_from("<H", self.data, 4)[0]

    def decode(self, sprite_id: int) -> Image.Image | None:
        if sprite_id <= 0 or sprite_id > self.count:
            return None
        address = struct.unpack_from("<I", self.data, 6 + (sprite_id - 1) * 4)[0]
        if address == 0:
            return None

        payload_size = struct.unpack_from("<H", self.data, address + 3)[0]
        payload = self.data[address + 5:address + 5 + payload_size]
        image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        pixels = image.load()
        cursor = 0
        pixel_index = 0

        while cursor + 4 <= len(payload) and pixel_index < 1024:
            transparent, colored = struct.unpack_from("<HH", payload, cursor)
            cursor += 4
            pixel_index += transparent
            for _ in range(colored):
                if cursor + 3 > len(payload) or pixel_index >= 1024:
                    break
                x = pixel_index % 32
                y = pixel_index // 32
                r, g, b = struct.unpack_from("<BBB", payload, cursor)
                pixels[x, y] = (r, g, b, 255)
                cursor += 3
                pixel_index += 1
        return image


def numeric_attr(element: ET.Element, name: str) -> int | None:
    value = element.get(name)
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def collect_rme_continuity_server_ids(rme_dir: Path) -> dict[int, set[str]]:
    reasons: dict[int, set[str]] = defaultdict(set)
    continuity_brush_types = {"border", "wall", "table", "carpet"}

    for xml_path in sorted(rme_dir.glob("*.xml")):
        root = ET.parse(xml_path).getroot()

        for border_item in root.iter("borderitem"):
            server_id = numeric_attr(border_item, "item")
            if server_id:
                reasons[server_id].add(f"{xml_path.name}:borderitem")

        for replacement in root.iter("replace_border"):
            server_id = numeric_attr(replacement, "with")
            if server_id:
                reasons[server_id].add(f"{xml_path.name}:replace_border")

        for carpet in root.iter("carpet"):
            server_id = numeric_attr(carpet, "id")
            if server_id:
                reasons[server_id].add(f"{xml_path.name}:carpet")

        for border in root.iter("border"):
            server_id = numeric_attr(border, "ground_equivalent")
            if server_id:
                reasons[server_id].add(f"{xml_path.name}:ground_equivalent")

        for composite in root.iter("composite"):
            for item in composite.iter("item"):
                server_id = numeric_attr(item, "id")
                if server_id:
                    reasons[server_id].add(f"{xml_path.name}:composite")

        for brush in root.iter("brush"):
            brush_type = brush.get("type", "")
            if brush_type not in continuity_brush_types:
                continue
            brush_name = brush.get("name", "sem-nome")
            for item in brush.iter("item"):
                server_id = numeric_attr(item, "id")
                if server_id:
                    reasons[server_id].add(f"{brush_type}:{brush_name}")
            for door in brush.iter("door"):
                server_id = numeric_attr(door, "id")
                if server_id:
                    reasons[server_id].add(f"{brush_type}:{brush_name}:door")

    return reasons


def block_folder(root: Path, sprite_id: int) -> Path:
    start = ((sprite_id - 1) // 1000) * 1000 + 1
    end = start + 999
    folder = root / f"{start:05d}-{end:05d}"
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export conservative one-tile HD candidates and classify continuity risks."
    )
    parser.add_argument("--dat", type=Path, default=DEFAULT_DAT)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--cwm", type=Path, default=DEFAULT_CWM)
    parser.add_argument("--otb", type=Path, default=DEFAULT_OTB)
    parser.add_argument("--rme-dir", type=Path, default=DEFAULT_RME)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove previously generated PNGs from the managed output folders before exporting.",
    )
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    flat_batch_dir = args.output / "00 ENVIAR AO UPSCAYL - TODOS JUNTOS - 32x"
    candidates_dir = args.output / "01 PARA UPSCALE - 32x"
    already_hd_dir = args.output / "02 JA HD - 64x"
    continuity_dir = args.output / "03 REVISAO - CONTINUIDADE - 32x"
    layered_dir = args.output / "03B REVISAO TECNICA - CAMADAS - 32x"
    multi_tile_dir = args.output / "04 FORA DO ESCOPO - MAIOR QUE 1 TILE - 32x"
    reports_dir = args.output / "05 INDICES E RELATORIOS"
    managed_image_dirs = (
        flat_batch_dir,
        candidates_dir,
        already_hd_dir,
        continuity_dir,
        layered_dir,
        multi_tile_dir,
    )
    for directory in (*managed_image_dirs, reports_dir):
        directory.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for directory in managed_image_dirs:
            pngs = directory.glob("*.png") if directory == flat_batch_dir else directory.rglob("*.png")
            for png in pngs:
                png.unlink()

    server_to_client = read_otb_mapping(args.otb)
    continuity_server_reasons = collect_rme_continuity_server_ids(args.rme_dir)
    continuity_client_reasons: dict[int, set[str]] = defaultdict(set)
    for server_id, reasons in continuity_server_reasons.items():
        client_id = server_to_client.get(server_id)
        if client_id:
            continuity_client_reasons[client_id].update(
                f"SID {server_id} {reason}" for reason in reasons
            )

    things_by_category = {
        category: parse_dat(args.dat, 772, category, None)
        for category in (
            THING_CATEGORY_ITEM,
            THING_CATEGORY_CREATURE,
            THING_CATEGORY_EFFECT,
            THING_CATEGORY_MISSILE,
        )
    }

    thing_rows: list[dict[str, object]] = []
    sprite_references: dict[int, list[dict[str, object]]] = defaultdict(list)
    for category, things in things_by_category.items():
        for client_id, thing in things.items():
            reasons: list[str] = []
            if thing.width != 1 or thing.height != 1:
                status = STATUS_MULTI_TILE
                reasons.append(f"geometry={thing.width}x{thing.height}")
            elif category == THING_CATEGORY_ITEM and THING_ATTR_GROUND in thing.attrs:
                status = STATUS_CONTINUITY
                reasons.append("DAT:ground")
            elif category == THING_CATEGORY_ITEM and client_id in continuity_client_reasons:
                status = STATUS_CONTINUITY
                reasons.extend(sorted(continuity_client_reasons[client_id]))
            elif thing.layers > 1:
                status = STATUS_LAYERED
                reasons.append(f"DAT:layers={thing.layers}")
            else:
                status = STATUS_CANDIDATE
                reasons.append("one-tile sem familia de continuidade detectada")

            unique_sprites = thing.unique_sprites
            thing_rows.append(
                {
                    "category": category_name(category),
                    "clientId": client_id,
                    "status": status,
                    "reason": " | ".join(reasons),
                    "width": thing.width,
                    "height": thing.height,
                    "layers": thing.layers,
                    "patternX": thing.pattern_x,
                    "patternY": thing.pattern_y,
                    "patternZ": thing.pattern_z,
                    "frames": thing.frames,
                    "orderedSpriteCount": len(thing.sprites),
                    "uniqueSpriteCount": len(unique_sprites),
                    "spriteIds": ",".join(str(sprite_id) for sprite_id in unique_sprites),
                }
            )
            for sprite_id in unique_sprites:
                sprite_references[sprite_id].append(
                    {
                        "category": category_name(category),
                        "clientId": client_id,
                        "thingStatus": status,
                        "reason": " | ".join(reasons),
                    }
                )

    _cwm_version, cwm_sprite_count, cwm_sprites = read_cwm(args.cwm)
    spr = SprArchive(args.spr)
    sprite_rows: list[dict[str, object]] = []
    counts: dict[str, int] = defaultdict(int)
    empty_sprites: list[int] = []

    for sprite_id, payload in sorted(cwm_sprites.items()):
        (block_folder(already_hd_dir, sprite_id) / f"{sprite_id}.png").write_bytes(payload)

    for sprite_id in sorted(sprite_references):
        references = sprite_references[sprite_id]
        reference_statuses = {str(reference["thingStatus"]) for reference in references}
        if sprite_id in MANUAL_CONTINUITY_SPRITE_IDS:
            status = STATUS_CONTINUITY
        elif STATUS_CONTINUITY in reference_statuses:
            status = STATUS_CONTINUITY
        elif STATUS_MULTI_TILE in reference_statuses:
            status = STATUS_MULTI_TILE
        elif STATUS_LAYERED in reference_statuses:
            status = STATUS_LAYERED
        elif sprite_id in cwm_sprites:
            status = STATUS_ALREADY_HD
        else:
            status = STATUS_CANDIDATE

        source_image = spr.decode(sprite_id)
        if source_image is None:
            empty_sprites.append(sprite_id)
            continue

        if status == STATUS_CANDIDATE:
            source_image.save(flat_batch_dir / f"{sprite_id}.png")
            source_image.save(block_folder(candidates_dir, sprite_id) / f"{sprite_id}.png")
        elif status == STATUS_CONTINUITY:
            source_image.save(block_folder(continuity_dir, sprite_id) / f"{sprite_id}.png")
        elif status == STATUS_LAYERED:
            source_image.save(block_folder(layered_dir, sprite_id) / f"{sprite_id}.png")
        elif status == STATUS_MULTI_TILE:
            source_image.save(block_folder(multi_tile_dir, sprite_id) / f"{sprite_id}.png")
        counts[status] += 1
        sprite_rows.append(
            {
                "spriteId": sprite_id,
                "status": status,
                "alreadyInCwm": sprite_id in cwm_sprites,
                "referenceCount": len(references),
                "categories": ",".join(sorted({str(reference["category"]) for reference in references})),
                "clientIds": ",".join(
                    str(client_id)
                    for client_id in sorted({int(reference["clientId"]) for reference in references})
                ),
                "reasons": " || ".join(
                    sorted(
                        {
                            *{str(reference["reason"]) for reference in references},
                            *(
                                {"exclusao manual: continuidade confirmada visualmente"}
                                if sprite_id in MANUAL_CONTINUITY_SPRITE_IDS
                                else set()
                            ),
                        }
                    )
                ),
            }
        )

    write_csv(
        reports_dir / "sprite-index.csv",
        [
            "spriteId",
            "status",
            "alreadyInCwm",
            "referenceCount",
            "categories",
            "clientIds",
            "reasons",
        ],
        sprite_rows,
    )
    write_csv(
        reports_dir / "thing-index.csv",
        [
            "category",
            "clientId",
            "status",
            "reason",
            "width",
            "height",
            "layers",
            "patternX",
            "patternY",
            "patternZ",
            "frames",
            "orderedSpriteCount",
            "uniqueSpriteCount",
            "spriteIds",
        ],
        thing_rows,
    )

    rme_rows = [
        {
            "serverId": server_id,
            "clientId": server_to_client.get(server_id, ""),
            "reasons": " | ".join(sorted(reasons)),
        }
        for server_id, reasons in sorted(continuity_server_reasons.items())
    ]
    write_csv(
        reports_dir / "rme-continuity-index.csv",
        ["serverId", "clientId", "reasons"],
        rme_rows,
    )

    summary = {
        "dat": str(args.dat),
        "spr": str(args.spr),
        "sprSignature": spr.signature,
        "sprCount": spr.count,
        "cwm": str(args.cwm),
        "cwmCountHeader": cwm_sprite_count,
        "cwmEntries": len(cwm_sprites),
        "activeCwmSpritesExported": len(cwm_sprites),
        "rmeDirectory": str(args.rme_dir),
        "output": str(args.output),
        "thingCounts": {
            category_name(category): len(things)
            for category, things in things_by_category.items()
        },
        "referencedUniqueSprites": len(sprite_references),
        "exportedCounts": dict(sorted(counts.items())),
        "emptyReferencedSprites": empty_sprites,
        "rules": [
            "Only DAT things with width=1 and height=1 enter the candidate pool.",
            "All DAT grounds are classified as continuity.",
            "RME border, wall, table and carpet brush members are classified as continuity.",
            "RME replace_border targets and direct carpet ids are classified as continuity.",
            "RME composite members are classified as continuity.",
            "One-tile things with multiple DAT layers are separated for technical review.",
            "Visually confirmed continuity sprite ranges are maintained as manual exclusions.",
            "A sprite shared with any continuity or multi-tile thing inherits the safer exclusion.",
            "Sprites already present in the active CWM are exported separately and are not candidates.",
        ],
    }
    (reports_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    readme = """LOTE AUTOMATICO DE SPRITES SIMPLES 7.72

00 ENVIAR AO UPSCAYL - TODOS JUNTOS - 32x
Copia plana de todas as candidatas. Selecione esta pasta no modo batch do Upscayl.
Mantenha os nomes numericos dos arquivos.

01 PARA UPSCALE - 32x
As mesmas candidatas separadas em blocos para navegacao e conferencia.
Os nomes sao Sprite IDs, nao Client IDs.

02 JA HD - 64x
Sprites que ja existem no Tibia.cwm ativo. Nao devem ser reprocessadas neste lote.

03 REVISAO - CONTINUIDADE - 32x
Grounds, borders, walls, tables, carpets, composites do RME e qualquer sprite
compartilhada com essas familias. Nao subir individualmente sem revisar/mosaico.

03B REVISAO TECNICA - CAMADAS - 32x
Things 1x1 compostas por mais de uma camada DAT. Inclui mascaras de cor de outfits;
o upscale comum pode alterar cores tecnicas ou criar halos entre camadas.

04 FORA DO ESCOPO - MAIOR QUE 1 TILE - 32x
Componentes de things cuja geometria DAT ocupa mais de um tile.

05 INDICES E RELATORIOS
- sprite-index.csv: Sprite ID -> status, categorias e Client IDs.
- thing-index.csv: categoria/Client ID -> geometria, patterns, frames e sprites.
- rme-continuity-index.csv: Server IDs do RME convertidos para Client IDs.
- summary.json: contagens e regras da classificacao.

Animacoes, direcoes, layers e patterns 1x1 permanecem no lote. O manifesto guarda
a ordem DAT; a reconstrucao do CWM usa os PNGs numericos por Sprite ID.
"""
    (args.output / "LEIA-ME.txt").write_text(readme, encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
