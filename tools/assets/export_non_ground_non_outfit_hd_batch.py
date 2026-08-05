#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import shutil
from collections import defaultdict
from pathlib import Path

from export_simple_hd_candidates import SprArchive
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
DEFAULT_OUTPUT = Path(
    r"C:\Users\guisu\OneDrive\Área de Trabalho"
    r"\LOTE GERAL RESTANTE - SEM OUTFITS E GROUNDS - 32x"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def block_name(sprite_id: int) -> str:
    start = ((sprite_id - 1) // 1000) * 1000 + 1
    end = start + 999
    return f"{start:05d}-{end:05d}"


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def collect_references(things_by_category: dict[int, dict[int, object]]) -> dict[int, list[dict[str, object]]]:
    references: dict[int, list[dict[str, object]]] = defaultdict(list)
    for category, things in things_by_category.items():
        for client_id, thing in things.items():
            for sprite_id in thing.unique_sprites:
                references[sprite_id].append(
                    {
                        "category": category_name(category),
                        "clientId": client_id,
                        "width": thing.width,
                        "height": thing.height,
                        "layers": thing.layers,
                        "patternX": thing.pattern_x,
                        "patternY": thing.pattern_y,
                        "patternZ": thing.pattern_z,
                        "frames": thing.frames,
                    }
                )
    return references


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export every remaining non-ground and non-outfit Sprite ID for individual upscale."
    )
    parser.add_argument("--dat", type=Path, default=DEFAULT_DAT)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--cwm", type=Path, default=DEFAULT_CWM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    flat_dir = args.output / "00 ENVIAR AO UPSCAYL - TODOS JUNTOS - 32x"
    blocks_dir = args.output / "01 CONFERENCIA POR BLOCOS - 32x"
    reports_dir = args.output / "02 INDICES E RELATORIOS"
    if args.clean and args.output.exists():
        shutil.rmtree(args.output)
    for directory in (flat_dir, blocks_dir, reports_dir):
        directory.mkdir(parents=True, exist_ok=True)

    things_by_category = {
        category: parse_dat(args.dat, 772, category, None)
        for category in (
            THING_CATEGORY_ITEM,
            THING_CATEGORY_CREATURE,
            THING_CATEGORY_EFFECT,
            THING_CATEGORY_MISSILE,
        )
    }
    items = things_by_category[THING_CATEGORY_ITEM]
    ground_items = {
        client_id: thing
        for client_id, thing in items.items()
        if THING_ATTR_GROUND in thing.attrs
    }
    non_ground_items = {
        client_id: thing
        for client_id, thing in items.items()
        if THING_ATTR_GROUND not in thing.attrs
    }
    included_things = {
        THING_CATEGORY_ITEM: non_ground_items,
        THING_CATEGORY_EFFECT: things_by_category[THING_CATEGORY_EFFECT],
        THING_CATEGORY_MISSILE: things_by_category[THING_CATEGORY_MISSILE],
    }

    all_references = collect_references(things_by_category)
    included_references = collect_references(included_things)
    ground_references = collect_references({THING_CATEGORY_ITEM: ground_items})
    outfit_references = collect_references(
        {THING_CATEGORY_CREATURE: things_by_category[THING_CATEGORY_CREATURE]}
    )
    _cwm_version, cwm_count, cwm_sprites = read_cwm(args.cwm)

    included_ids = set(included_references)
    ground_ids = set(ground_references)
    outfit_ids = set(outfit_references)
    active_hd_ids = set(cwm_sprites)
    candidate_ids = sorted(included_ids - ground_ids - outfit_ids - active_hd_ids)

    archive = SprArchive(args.spr)
    candidate_rows: list[dict[str, object]] = []
    empty_ids: list[int] = []
    for sprite_id in candidate_ids:
        image = archive.decode(sprite_id)
        if image is None:
            empty_ids.append(sprite_id)
            continue

        flat_path = flat_dir / f"{sprite_id}.png"
        image.save(flat_path)
        block_dir = blocks_dir / block_name(sprite_id)
        block_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(flat_path, block_dir / flat_path.name)

        references = included_references[sprite_id]
        candidate_rows.append(
            {
                "spriteId": sprite_id,
                "categories": ",".join(sorted({str(row["category"]) for row in references})),
                "clientIds": ",".join(
                    str(client_id)
                    for client_id in sorted({int(row["clientId"]) for row in references})
                ),
                "referenceCount": len(references),
                "geometries": ",".join(
                    sorted({f'{row["width"]}x{row["height"]}' for row in references})
                ),
                "maxLayers": max(int(row["layers"]) for row in references),
                "maxFrames": max(int(row["frames"]) for row in references),
            }
        )

    excluded_rows: list[dict[str, object]] = []
    for sprite_id in sorted(set(all_references) - set(candidate_ids)):
        reasons: list[str] = []
        if sprite_id in ground_ids:
            reasons.append("ground")
        if sprite_id in outfit_ids:
            reasons.append("outfit/creature")
        if sprite_id in active_hd_ids:
            reasons.append("already-in-active-cwm")
        if sprite_id not in included_ids:
            reasons.append("not-referenced-by-non-ground-item-effect-missile")
        references = all_references[sprite_id]
        excluded_rows.append(
            {
                "spriteId": sprite_id,
                "reasons": ",".join(reasons),
                "categories": ",".join(sorted({str(row["category"]) for row in references})),
                "clientIds": ",".join(
                    str(client_id)
                    for client_id in sorted({int(row["clientId"]) for row in references})
                ),
            }
        )

    write_csv(
        reports_dir / "sprites-para-upscale.csv",
        [
            "spriteId",
            "categories",
            "clientIds",
            "referenceCount",
            "geometries",
            "maxLayers",
            "maxFrames",
        ],
        candidate_rows,
    )
    write_csv(
        reports_dir / "sprites-excluidas.csv",
        ["spriteId", "reasons", "categories", "clientIds"],
        excluded_rows,
    )

    summary = {
        "dat": str(args.dat),
        "datSha256": sha256(args.dat),
        "spr": str(args.spr),
        "sprSha256": sha256(args.spr),
        "sprSignature": archive.signature,
        "sprCount": archive.count,
        "cwm": str(args.cwm),
        "cwmSha256": sha256(args.cwm),
        "cwmCountHeader": cwm_count,
        "cwmEntries": len(cwm_sprites),
        "output": str(args.output),
        "thingCounts": {
            category_name(category): len(things)
            for category, things in things_by_category.items()
        },
        "groundThingCount": len(ground_items),
        "nonGroundItemCount": len(non_ground_items),
        "uniqueReferencedByEligibleCategories": len(included_ids),
        "uniqueGroundSprites": len(ground_ids),
        "uniqueOutfitSprites": len(outfit_ids),
        "sharedEligibleAndGround": len(included_ids & ground_ids),
        "sharedEligibleAndOutfit": len(included_ids & outfit_ids),
        "alreadyHdAmongEligible": len(included_ids & active_hd_ids),
        "candidateSpriteCount": len(candidate_rows),
        "emptyCandidateSpriteIds": empty_ids,
        "rules": [
            "Include Sprite IDs referenced by non-ground items, effects or missiles.",
            "Exclude the complete DAT creature category (outfits).",
            "Exclude every Sprite ID referenced by any DAT item with the ground attribute.",
            "Exclude every Sprite ID already present in the active CWM.",
            "A shared Sprite ID inherits ground or outfit exclusion.",
            "Geometry, layers, patterns, animation and continuity do not exclude a non-ground sprite.",
            "File names are Sprite IDs, not Client IDs.",
        ],
    }
    (reports_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (args.output / "LEIA-ME.txt").write_text(
        "LOTE GERAL RESTANTE - SEM OUTFITS E GROUNDS\n\n"
        "Use a pasta 00 como entrada do Upscayl. Ela contem uma copia plana de todos\n"
        "os Sprite IDs restantes. Mantenha os nomes numericos dos PNGs.\n\n"
        "A pasta 01 contem os mesmos PNGs separados em blocos de 1.000 apenas para\n"
        "conferencia visual. Nao envie as duas pastas ao Upscayl.\n\n"
        "Exclusoes:\n"
        "- toda a categoria creature do DAT (outfits);\n"
        "- toda sprite usada por item com atributo ground;\n"
        "- toda sprite que ja existe no Tibia.cwm ativo.\n\n"
        "Sprites grandes, animadas, com layers, patterns ou continuidade permanecem\n"
        "neste lote. O teste das arvores confirmou o processamento individual.\n\n"
        "Os nomes dos arquivos sao Sprite IDs, nao Client IDs.\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
