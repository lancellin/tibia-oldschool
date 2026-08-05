#!/usr/bin/env python3
import argparse
import csv
import json
import shutil
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

from extract_map_sprites import read_otb_mapping
from extract_thing_assets import THING_CATEGORY_ITEM, parse_dat
from merge_cwm import read_cwm, write_cwm


GROUPS = {
    "mesas-e-counters": {
        "serverIds": list(range(1602, 1640)),
        "reason": "Ficaram feios; refazer com outro modelo do Upscayl.",
        "expansion": "Bloco contiguo de mesas, counters e respectivas variacoes no RME.",
        "examples": [2325, 2322, 2338, 2331, 2332, 2330, 2320],
    },
    "partes-da-piramide": {
        "serverIds": [459, *range(1398, 1406), *range(1550, 1560)],
        "reason": "Partes da piramide ficaram feias; refazer com outro modelo.",
        "expansion": "Brush pyramid ramp mais bloco contiguo das pecas 2189-2198.",
        "examples": [1964, 1965, 1963, 1962, 1967, 2191, 2197, 2193, 469],
    },
    "corrimao-de-madeira-do-barco": {
        "serverIds": list(range(1514, 1522)),
        "reason": "Corrimao ficou feio; refazer com outro modelo.",
        "expansion": "Bloco contiguo do tileset ao redor dos exemplos informados.",
        "examples": [2156, 2154, 2157],
    },
    "paredes-de-ankrahmun": {
        "serverIds": list(range(1100, 1111)),
        "reason": "Paredes ficaram ruins; refazer como familia.",
        "expansion": "Brush completo egypt stone wall do RME.",
        "examples": [1346, 1349, 1345],
    },
    "todas-as-portas": {
        "serverIds": [],
        "reason": "Portas abertas, fechadas e molduras devem ser refeitas em conjunto.",
        "expansion": "Todos os elementos door de todos os wall brushes do RME.",
        "examples": [1652, 1651, 1629, 1630, 1632, 1633, 1640, 1641],
    },
    "walls-de-pedra": {
        "serverIds": list(range(897, 917)),
        "reason": "Walls ficaram feias ou com continuidade ruim.",
        "expansion": "Bloco completo ao redor do brush rock wall e exemplos informados.",
        "examples": [1108, 1113, 1112, 1114, 1125, 1118, 1120],
    },
    "gray-rock-cid-1791": {
        "serverIds": [1304],
        "reason": "Sem erro tecnico, mas o resultado visual ficou feio.",
        "expansion": "Somente o CID explicitamente informado.",
        "examples": [1791],
    },
    "borda-parede-1081-1088": {
        "serverIds": list(range(873, 881)),
        "reason": "Divisao marcada e falta de continuidade.",
        "expansion": "Familia completa do border/tileset contiguo.",
        "examples": [1085, 1082, 1081],
    },
    "pedras-grandes-4459-4466": {
        "serverIds": list(range(4470, 4478)),
        "reason": "Pedras grandes ficaram feias.",
        "expansion": "Familia completa de oito borderitems contiguos no RME.",
        "examples": [4460, 4462, 4464, 4465],
    },
    "rampas": {
        "serverIds": [459, *range(1388, 1396)],
        "reason": "Rampas devem ser refeitas com outro modelo.",
        "expansion": "Brush completo ramp do RME.",
        "examples": [1951, 1952, 1953],
    },
    "plaster-wall": {
        "serverIds": [
            *range(1036, 1049),
            *range(1209, 1231),
            1263,
            1264,
        ],
        "reason": "Familia com variacoes ruins; refazer em conjunto.",
        "expansion": "Brush completo plaster wall do RME, incluindo portas e variacoes.",
        "examples": [1283, 1282, 1281, 1735],
    },
    "stone-wall": {
        "serverIds": [*range(1049, 1060), 1267, 1268],
        "reason": "Familia com variacoes ruins; refazer em conjunto.",
        "expansion": "Brush completo stone wall do RME.",
        "examples": [1295, 1297, 1301, 1294, 1739],
    },
    "stone-railing": {
        "serverIds": [1524, 1526, 1528, 1530],
        "reason": "Familia associada ao CID 2162 deve ser refeita.",
        "expansion": "Brush completo stone railing do RME.",
        "examples": [2162],
    },
    "bookcases-2435-2440": {
        "serverIds": list(range(1718, 1724)),
        "reason": "Bookcases ficaram ruins; refazer como familia.",
        "expansion": "Brush bookcase do RME mais o CID 2440 adjacente no tileset.",
        "examples": [2439, 2436, 2440],
    },
    "cid-2126": {
        "serverIds": [1495],
        "reason": "Resultado visual ficou feio.",
        "expansion": "Somente o CID explicitamente informado.",
        "examples": [2126],
    },
}


def all_rme_doors(rme_dir: Path) -> set[int]:
    result: set[int] = set()
    for path in rme_dir.glob("*.xml"):
        root = ET.parse(path).getroot()
        for door in root.iter("door"):
            value = door.get("id")
            if value and value.isdigit():
                result.add(int(value))
    return result


def copy_sprite_files(
    sprite_ids: set[int],
    sources: list[Path],
    destination: Path,
) -> int:
    copied = 0
    destination.mkdir(parents=True, exist_ok=True)
    for sprite_id in sorted(sprite_ids):
        for source in sources:
            source_path = source / f"{sprite_id}.png"
            if source_path.exists():
                shutil.copy2(source_path, destination / source_path.name)
                copied += 1
                break
    return copied


def main() -> None:
    parser = argparse.ArgumentParser(description="Register and selectively roll back rejected general-batch families.")
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--otb", type=Path, required=True)
    parser.add_argument("--rme-dir", type=Path, required=True)
    parser.add_argument("--active-cwm", type=Path, required=True)
    parser.add_argument("--baseline-cwm", type=Path, required=True)
    parser.add_argument("--batch-index", type=Path, required=True)
    parser.add_argument("--original-pngs", type=Path, required=True)
    parser.add_argument("--raw-upscaled-pngs", type=Path, required=True)
    parser.add_argument("--ready-pngs", type=Path, required=True)
    parser.add_argument("--previous-original-pngs", type=Path)
    parser.add_argument("--previous-raw-upscaled-pngs", type=Path)
    parser.add_argument("--previous-ready-pngs", type=Path)
    parser.add_argument("--permanent-dir", type=Path, required=True)
    parser.add_argument("--output-cwm", type=Path, required=True)
    parser.add_argument("--registry-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.registry_dir.exists():
        shutil.rmtree(args.registry_dir)
    server_to_client = read_otb_mapping(args.otb)
    items = parse_dat(args.dat, 772, THING_CATEGORY_ITEM, None)
    with args.batch_index.open(encoding="utf-8-sig") as input_file:
        general_batch_ids = {int(row["spriteId"]) for row in csv.DictReader(input_file)}

    groups = json.loads(json.dumps(GROUPS))
    groups["todas-as-portas"]["serverIds"] = sorted(all_rme_doors(args.rme_dir))

    _av, active_header, active = read_cwm(args.active_cwm)
    baseline_version, baseline_header, baseline = read_cwm(args.baseline_cwm)
    if active_header != baseline_header:
        raise SystemExit("CWM header mismatch")
    if not set(baseline) <= set(active):
        raise SystemExit("The active CWM does not contain the complete baseline")
    if any(active[sprite_id] != payload for sprite_id, payload in baseline.items()):
        raise SystemExit("At least one baseline payload changed in the active CWM")

    permanent_sprite_ids = {
        int(path.stem)
        for path in args.permanent_dir.rglob("*.png")
        if path.stem.isdigit()
    }
    args.registry_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []
    rollback_sprite_ids: set[int] = set()
    group_summaries: list[dict[str, object]] = []
    sprite_to_groups: dict[int, set[str]] = defaultdict(set)

    for group_name, group in groups.items():
        server_ids = sorted(set(int(value) for value in group["serverIds"]))
        client_ids = sorted(
            {
                server_to_client[server_id]
                for server_id in server_ids
                if server_id in server_to_client
            }
        )
        sprite_ids = sorted(
            {
                sprite_id
                for client_id in client_ids
                if client_id in items
                for sprite_id in items[client_id].unique_sprites
            }
        )
        batch_sprite_ids = sorted(set(sprite_ids) & general_batch_ids)
        active_family_ids = set(sprite_ids) & set(active)
        removable_ids = sorted(active_family_ids - permanent_sprite_ids)
        preserved_permanent_ids = sorted(active_family_ids & permanent_sprite_ids)
        removed_from_baseline_ids = sorted(set(removable_ids) & set(baseline))
        rollback_sprite_ids.update(removable_ids)
        for sprite_id in removable_ids:
            sprite_to_groups[sprite_id].add(group_name)

        group_dir = args.registry_dir / "grupos" / group_name
        original_count = copy_sprite_files(
            set(removable_ids),
            [
                args.original_pngs,
                *([args.previous_original_pngs] if args.previous_original_pngs else []),
            ],
            group_dir / "00 ORIGINAL 32x",
        )
        raw_count = copy_sprite_files(
            set(removable_ids),
            [
                args.raw_upscaled_pngs,
                *(
                    [args.previous_raw_upscaled_pngs]
                    if args.previous_raw_upscaled_pngs
                    else []
                ),
            ],
            group_dir / "01 UPSCALE REPROVADO RAW 64x",
        )
        ready_count = copy_sprite_files(
            set(removable_ids),
            [
                args.ready_pngs,
                *([args.previous_ready_pngs] if args.previous_ready_pngs else []),
            ],
            group_dir / "02 UPSCALE REPROVADO ALPHA RESTAURADO 64x",
        )

        summary = {
            "group": group_name,
            "reason": group["reason"],
            "expansion": group["expansion"],
            "exampleClientIds": group["examples"],
            "serverIds": server_ids,
            "clientIds": client_ids,
            "allReferencedSpriteIds": sprite_ids,
            "generalBatchSpriteIds": batch_sprite_ids,
            "rolledBackSpriteIds": removable_ids,
            "removedFromBaselineSpriteIds": removed_from_baseline_ids,
            "preservedPermanentSpriteIds": preserved_permanent_ids,
            "filesCopied": {
                "original32": original_count,
                "rawUpscaled64": raw_count,
                "readyUpscaled64": ready_count,
            },
        }
        group_summaries.append(summary)
        (group_dir / "manifest.json").write_text(
            json.dumps(summary, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )

        for client_id in client_ids:
            thing = items.get(client_id)
            if thing is None:
                continue
            for sprite_id in thing.unique_sprites:
                rows.append(
                    {
                        "group": group_name,
                        "reason": group["reason"],
                        "expansion": group["expansion"],
                        "serverIds": ",".join(
                            str(server_id)
                            for server_id in server_ids
                            if server_to_client.get(server_id) == client_id
                        ),
                        "clientId": client_id,
                        "spriteId": sprite_id,
                        "wasInGeneralBatch": sprite_id in general_batch_ids,
                        "wasInBaseline": sprite_id in baseline,
                        "isPermanent": sprite_id in permanent_sprite_ids,
                        "rolledBack": sprite_id in removable_ids,
                    }
                )

    expected_general_entries = set(active) - set(baseline)
    if expected_general_entries != general_batch_ids:
        raise SystemExit(
            "Active CWM additions no longer match the recorded general batch: "
            f"activeOnly={len(expected_general_entries)} index={len(general_batch_ids)}"
        )

    final = {
        sprite_id: payload
        for sprite_id, payload in active.items()
        if sprite_id not in rollback_sprite_ids
    }
    write_cwm(
        args.output_cwm,
        baseline_version,
        max(active_header, baseline_header),
        final,
    )

    registry = {
        "status": "rolled-back-from-general-batch",
        "nextAction": "Refazer cada grupo com outro modelo do Upscayl e testar separadamente.",
        "activeEntriesBefore": len(active),
        "baselineEntries": len(baseline),
        "generalBatchEntries": len(general_batch_ids),
        "uniqueRolledBackSpriteCount": len(rollback_sprite_ids),
        "rolledBackFromBaselineSpriteIds": sorted(set(rollback_sprite_ids) & set(baseline)),
        "preservedPermanentSpriteIds": sorted(
            {
                sprite_id
                for summary in group_summaries
                for sprite_id in summary["preservedPermanentSpriteIds"]
            }
        ),
        "activeEntriesAfter": len(final),
        "rolledBackSpriteIds": sorted(rollback_sprite_ids),
        "spriteGroups": {
            str(sprite_id): sorted(group_names)
            for sprite_id, group_names in sorted(sprite_to_groups.items())
        },
        "groups": group_summaries,
    }
    (args.registry_dir / "registry.json").write_text(
        json.dumps(registry, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    with (args.registry_dir / "registry.csv").open(
        "w",
        newline="",
        encoding="utf-8-sig",
    ) as output:
        writer = csv.DictWriter(
            output,
            fieldnames=[
                "group",
                "reason",
                "expansion",
                "serverIds",
                "clientId",
                "spriteId",
                "wasInGeneralBatch",
                "wasInBaseline",
                "isPermanent",
                "rolledBack",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    markdown = [
        "# Registro de retrabalho do lote geral HD",
        "",
        "Status: overrides revertidos do lote geral.",
        "",
        "Proxima acao: refazer cada grupo com outro modelo do Upscayl e testar separadamente.",
        "",
        f"- Entradas antes: {len(active)}",
        f"- Baseline preservada: {len(baseline)}",
        f"- Sprites unicas revertidas: {len(rollback_sprite_ids)}",
        f"- Sprites anteriores tambem revertidas: {len(set(rollback_sprite_ids) & set(baseline))}",
        "- Sprites permanentes preservadas: "
        + str(
            len(
                {
                    sprite_id
                    for summary in group_summaries
                    for sprite_id in summary["preservedPermanentSpriteIds"]
                }
            )
        ),
        f"- Entradas depois: {len(final)}",
        "",
        "## Grupos",
        "",
    ]
    for summary in group_summaries:
        markdown.extend(
            [
                f"### {summary['group']}",
                "",
                f"- Motivo: {summary['reason']}",
                f"- Expansao: {summary['expansion']}",
                "- CIDs de exemplo: "
                + ", ".join(str(value) for value in summary["exampleClientIds"]),
                f"- CIDs expandidos: {len(summary['clientIds'])}",
                f"- Sprite IDs revertidos: {len(summary['rolledBackSpriteIds'])}",
                f"- Sprites permanentes preservadas: {len(summary['preservedPermanentSpriteIds'])}",
                "",
            ]
        )
    (args.registry_dir / "LEIA-ME.md").write_text(
        "\n".join(markdown),
        encoding="utf-8",
    )
    print(json.dumps(registry, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
