#!/usr/bin/env python3
import argparse
import ast
import json
import math
import random
import re
import shutil
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw

from extract_map_sprites import read_otb_mapping
from extract_sprites import decode_sprite
from extract_thing_assets import THING_CATEGORY_ITEM, ThingInfo, parse_dat


DEFAULT_RME_ROOT = Path(r"D:\tibia-oldschool\sources\rme-otacademy")
DEFAULT_DAT = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.dat")
DEFAULT_SPR = Path(r"D:\tibia-oldschool\sources\otclient-redemption\data\things\772\Tibia.spr")
DEFAULT_OUT = Path(r"D:\tibia-oldschool\tools\assets\work\rme-native-border-mosaics")

BORDER_VALUES = {
    "BORDER_NONE": 0,
    "NORTH_HORIZONTAL": 1,
    "EAST_HORIZONTAL": 2,
    "SOUTH_HORIZONTAL": 3,
    "WEST_HORIZONTAL": 4,
    "NORTHWEST_CORNER": 5,
    "NORTHEAST_CORNER": 6,
    "SOUTHWEST_CORNER": 7,
    "SOUTHEAST_CORNER": 8,
    "NORTHWEST_DIAGONAL": 9,
    "NORTHEAST_DIAGONAL": 10,
    "SOUTHEAST_DIAGONAL": 11,
    "SOUTHWEST_DIAGONAL": 12,
}

TILE_VALUES = {
    "TILE_NORTHWEST": 1,
    "TILE_NORTH": 2,
    "TILE_NORTHEAST": 4,
    "TILE_WEST": 8,
    "TILE_EAST": 16,
    "TILE_SOUTHWEST": 32,
    "TILE_SOUTH": 64,
    "TILE_SOUTHEAST": 128,
}

EDGE_TO_BORDER = {
    "n": BORDER_VALUES["NORTH_HORIZONTAL"],
    "e": BORDER_VALUES["EAST_HORIZONTAL"],
    "s": BORDER_VALUES["SOUTH_HORIZONTAL"],
    "w": BORDER_VALUES["WEST_HORIZONTAL"],
    "cnw": BORDER_VALUES["NORTHWEST_CORNER"],
    "cne": BORDER_VALUES["NORTHEAST_CORNER"],
    "csw": BORDER_VALUES["SOUTHWEST_CORNER"],
    "cse": BORDER_VALUES["SOUTHEAST_CORNER"],
    "dnw": BORDER_VALUES["NORTHWEST_DIAGONAL"],
    "dne": BORDER_VALUES["NORTHEAST_DIAGONAL"],
    "dse": BORDER_VALUES["SOUTHEAST_DIAGONAL"],
    "dsw": BORDER_VALUES["SOUTHWEST_DIAGONAL"],
}
TYPE_TO_EDGE = {value: key for key, value in EDGE_TO_BORDER.items()}

NEIGHBOURS = (
    (-1, -1, TILE_VALUES["TILE_NORTHWEST"]),
    (0, -1, TILE_VALUES["TILE_NORTH"]),
    (1, -1, TILE_VALUES["TILE_NORTHEAST"]),
    (-1, 0, TILE_VALUES["TILE_WEST"]),
    (1, 0, TILE_VALUES["TILE_EAST"]),
    (-1, 1, TILE_VALUES["TILE_SOUTHWEST"]),
    (0, 1, TILE_VALUES["TILE_SOUTH"]),
    (1, 1, TILE_VALUES["TILE_SOUTHEAST"]),
)

DIAGONAL_FALLBACK = {
    BORDER_VALUES["NORTHWEST_DIAGONAL"]: (
        BORDER_VALUES["WEST_HORIZONTAL"],
        BORDER_VALUES["NORTH_HORIZONTAL"],
    ),
    BORDER_VALUES["NORTHEAST_DIAGONAL"]: (
        BORDER_VALUES["EAST_HORIZONTAL"],
        BORDER_VALUES["NORTH_HORIZONTAL"],
    ),
    BORDER_VALUES["SOUTHWEST_DIAGONAL"]: (
        BORDER_VALUES["SOUTH_HORIZONTAL"],
        BORDER_VALUES["WEST_HORIZONTAL"],
    ),
    BORDER_VALUES["SOUTHEAST_DIAGONAL"]: (
        BORDER_VALUES["SOUTH_HORIZONTAL"],
        BORDER_VALUES["EAST_HORIZONTAL"],
    ),
}


@dataclass
class BorderDefinition:
    key: str
    group: int
    tiles: dict[int, int]


@dataclass
class BorderBlock:
    border: BorderDefinition | None
    outer: bool
    to: str
    super_border: bool
    specific_count: int


@dataclass
class GroundBrush:
    name: str
    z_order: int = 0
    items: list[tuple[int, int]] = field(default_factory=list)
    borders: list[BorderBlock] = field(default_factory=list)
    friends: set[str] = field(default_factory=set)
    hate_friends: bool = False
    has_optional_border: bool = False
    use_only_optional: bool = False

    def has_outer_border(self) -> bool:
        return self.has_optional_border or any(block.outer and block.to != "none" for block in self.borders)

    def has_inner_border(self) -> bool:
        return any(not block.outer and block.to != "none" for block in self.borders)

    def friend_of(self, other: "GroundBrush") -> bool:
        matched = other.name in self.friends or "all" in self.friends
        return not self.hate_friends if matched else self.hate_friends


class ExpressionEvaluator(ast.NodeVisitor):
    def __init__(self, names: dict[str, int]) -> None:
        self.names = names

    def visit_Expression(self, node: ast.Expression) -> int:
        return self.visit(node.body)

    def visit_Constant(self, node: ast.Constant) -> int:
        if not isinstance(node.value, int):
            raise ValueError(f"Unsupported constant: {node.value!r}")
        return node.value

    def visit_Name(self, node: ast.Name) -> int:
        if node.id not in self.names:
            raise ValueError(f"Unknown RME constant: {node.id}")
        return self.names[node.id]

    def visit_BinOp(self, node: ast.BinOp) -> int:
        left = self.visit(node.left)
        right = self.visit(node.right)
        if isinstance(node.op, ast.BitOr):
            return left | right
        if isinstance(node.op, ast.LShift):
            return left << right
        raise ValueError(f"Unsupported operator: {type(node.op).__name__}")

    def generic_visit(self, node: ast.AST) -> int:
        raise ValueError(f"Unsupported expression node: {type(node).__name__}")


def evaluate_expression(expression: str, names: dict[str, int]) -> int:
    tree = ast.parse(expression.strip(), mode="eval")
    return ExpressionEvaluator(names).visit(tree)


def load_rme_border_table(path: Path) -> list[list[int]]:
    source = path.read_text(encoding="utf-8")
    start = source.index("void GroundBrush::init()")
    end = source.index("void WallBrush::init()", start)
    section = source[start:end]
    assignment_re = re.compile(
        r"GroundBrush::border_types\[(.*?)\]\s*(?://[^\n]*)?\s*=\s*(.*?);",
        re.DOTALL,
    )
    names = {**TILE_VALUES, **BORDER_VALUES}
    packed: dict[int, int] = {}
    for match in assignment_re.finditer(section):
        index = evaluate_expression(match.group(1), names)
        value = evaluate_expression(match.group(2), names)
        packed[index] = value

    missing = sorted(set(range(256)) - set(packed))
    if missing:
        raise ValueError(f"RME border table is incomplete; missing masks: {missing}")

    table: list[list[int]] = []
    for mask in range(256):
        value = packed[mask]
        directions = []
        for shift in (0, 8, 16, 24):
            direction = (value >> shift) & 0xFF
            if direction == BORDER_VALUES["BORDER_NONE"]:
                break
            directions.append(direction)
        table.append(directions)
    return table


def parse_border_definition(node: ET.Element, key: str) -> BorderDefinition:
    tiles: dict[int, int] = {}
    for item in node.findall("borderitem"):
        edge = item.attrib.get("edge")
        if edge not in EDGE_TO_BORDER:
            continue
        tiles[EDGE_TO_BORDER[edge]] = int(item.attrib["item"])
    return BorderDefinition(
        key=key,
        group=int(node.attrib.get("group", 0)),
        tiles=tiles,
    )


def load_global_borders(path: Path) -> dict[int, BorderDefinition]:
    root = ET.parse(path).getroot()
    result: dict[int, BorderDefinition] = {}
    for node in root.findall("border"):
        border_id = int(node.attrib["id"])
        result[border_id] = parse_border_definition(node, str(border_id))
    return result


def load_ground_brushes(path: Path, global_borders: dict[int, BorderDefinition]) -> dict[str, GroundBrush]:
    root = ET.parse(path).getroot()
    brushes: dict[str, GroundBrush] = {}
    for node in root.findall("brush"):
        name = node.attrib.get("name")
        if not name or node.attrib.get("type") not in ("border", "ground"):
            continue
        brush = brushes.setdefault(name, GroundBrush(name=name))
        if "z-order" in node.attrib:
            brush.z_order = int(node.attrib["z-order"])
        if "solo_optional" in node.attrib:
            brush.use_only_optional = node.attrib["solo_optional"].lower() == "true"

        for child in node:
            if child.tag == "item":
                brush.items.append((int(child.attrib["id"]), int(child.attrib.get("chance", 0))))
            elif child.tag == "border":
                border: BorderDefinition | None
                if "id" in child.attrib:
                    border_id = int(child.attrib["id"])
                    border = None if border_id == 0 else global_borders.get(border_id)
                    if border_id and border is None:
                        raise ValueError(f"Brush {name} references unknown border {border_id}")
                elif "ground_equivalent" in child.attrib:
                    border = parse_border_definition(child, f"{name}:inline")
                else:
                    continue
                brush.borders.append(
                    BorderBlock(
                        border=border,
                        outer=child.attrib.get("align", "outer") != "inner",
                        to=child.attrib.get("to", "all"),
                        super_border=child.attrib.get("super", "false").lower() == "true",
                        specific_count=len(child.findall("specific")),
                    )
                )
            elif child.tag == "optional":
                brush.has_optional_border = True
            elif child.tag == "friend":
                friend = child.attrib.get("name")
                if friend:
                    brush.friends.add(friend)
                    brush.hate_friends = False
            elif child.tag == "enemy":
                enemy = child.attrib.get("name")
                if enemy:
                    brush.friends.add(enemy)
                    brush.hate_friends = True
            elif child.tag == "clear_borders":
                brush.borders.clear()
            elif child.tag == "clear_friends":
                brush.friends.clear()
                brush.hate_friends = False
    return brushes


def get_brush_to(first: GroundBrush, second: GroundBrush) -> BorderBlock | None:
    if first.z_order < second.z_order and second.has_outer_border():
        if first.has_inner_border():
            for block in first.borders:
                if not block.outer and block.to in (second.name, "all"):
                    return block
        for block in second.borders:
            if block.outer and block.to in (first.name, "all"):
                return block
    elif first.has_inner_border():
        for block in first.borders:
            if not block.outer and block.to in (second.name, "all"):
                return block
    return None


def select_normal_border(first: GroundBrush, second: GroundBrush) -> BorderBlock | None:
    if not (second.has_outer_border() or first.has_inner_border()):
        return None
    if second.friend_of(first) or first.friend_of(second):
        return None
    return get_brush_to(first, second)


def owner_for_block(
    block: BorderBlock | None,
    first: GroundBrush,
    second: GroundBrush,
) -> GroundBrush | None:
    if block is None:
        return None
    if any(candidate is block for candidate in first.borders):
        return first
    if any(candidate is block for candidate in second.borders):
        return second
    return None


def choose_ground_item(brush: GroundBrush, rng: random.Random) -> int:
    total = sum(chance for _server_id, chance in brush.items)
    if not brush.items:
        raise ValueError(f"Brush has no ground items: {brush.name}")
    if total <= 0:
        return brush.items[0][0]

    roll = rng.randint(1, total)
    cumulative = 0
    for server_id, chance in brush.items:
        cumulative += chance
        if roll < cumulative:
            return server_id
    return brush.items[0][0]


def sprite_index(thing: ThingInfo, frame: int, z: int, px: int, py: int, layer: int) -> int:
    index = frame
    index = index * thing.pattern_z + z
    index = index * thing.pattern_y + py
    index = index * thing.pattern_x + px
    index = index * thing.layers + layer
    index = index * thing.height
    index = index * thing.width
    return index


class ItemRenderer:
    def __init__(
        self,
        server_to_client: dict[int, int],
        things: dict[int, ThingInfo],
        spr_path: Path,
        source_dir: Path,
    ) -> None:
        self.server_to_client = server_to_client
        self.things = things
        self.spr_path = spr_path
        self.source_dir = source_dir
        self.sprite_cache: dict[int, Image.Image] = {}

    def decode(self, sprite_id: int) -> Image.Image:
        if sprite_id not in self.sprite_cache:
            image = decode_sprite(self.spr_path, sprite_id, 32, False, False)
            if image is None:
                raise ValueError(f"Sprite not found: {sprite_id}")
            image = image.convert("RGBA")
            self.sprite_cache[sprite_id] = image
            image.save(self.source_dir / f"{sprite_id}.png")
        return self.sprite_cache[sprite_id]

    def render(self, server_id: int, tile_x: int, tile_y: int) -> tuple[Image.Image, int, list[int]]:
        client_id = self.server_to_client.get(server_id)
        if client_id is None:
            raise ValueError(f"Server id {server_id} has no client mapping in items.otb")
        thing = self.things.get(client_id)
        if thing is None:
            raise ValueError(f"Client id {client_id} was not loaded from Tibia.dat")
        if thing.width != 1 or thing.height != 1:
            raise ValueError(
                f"Server id {server_id}/client id {client_id} is {thing.width}x{thing.height}; "
                "this border mosaic currently supports 1x1 tile items"
            )

        px = tile_x % thing.pattern_x
        py = tile_y % thing.pattern_y
        result = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        sprite_ids = []
        for layer in range(thing.layers):
            index = sprite_index(thing, 0, 0, px, py, layer)
            sprite_id = int(thing.sprites[index])
            if sprite_id <= 0:
                continue
            result.alpha_composite(self.decode(sprite_id))
            sprite_ids.append(sprite_id)
        return result, client_id, sprite_ids


def scaled(value: int, grid: int) -> int:
    return round(value * (grid - 1) / 23)


def build_brush_field(grid: int) -> list[list[bool]]:
    field = [[False for _ in range(grid)] for _ in range(grid)]

    def fill_rect(left: int, top: int, right: int, bottom: int, value: bool) -> None:
        left, top, right, bottom = (scaled(number, grid) for number in (left, top, right, bottom))
        for y in range(max(0, top), min(grid - 1, bottom) + 1):
            for x in range(max(0, left), min(grid - 1, right) + 1):
                field[y][x] = value

    fill_rect(2, 2, 11, 10, True)
    fill_rect(14, 2, 21, 9, True)
    fill_rect(3, 14, 10, 21, True)
    fill_rect(14, 13, 21, 21, True)
    fill_rect(5, 5, 7, 7, False)
    fill_rect(17, 4, 19, 6, False)
    fill_rect(6, 17, 8, 19, False)
    fill_rect(17, 16, 19, 18, False)

    # Small diagonal features exercise masks that rectangles alone do not produce.
    for x, y in ((12, 4), (13, 5), (11, 17), (12, 16), (13, 15)):
        field[scaled(y, grid)][scaled(x, grid)] = True
    return field


def neighbour_mask(field: list[list[bool]], tile_x: int, tile_y: int) -> int:
    grid = len(field)
    current = field[tile_y][tile_x]
    mask = 0
    for dx, dy, bit in NEIGHBOURS:
        x = tile_x + dx
        y = tile_y + dy
        neighbour = current if x < 0 or y < 0 or x >= grid or y >= grid else field[y][x]
        if neighbour != current:
            mask |= bit
    return mask


def expand_border_direction(border: BorderDefinition, direction: int) -> list[tuple[int, int]]:
    server_id = border.tiles.get(direction)
    if server_id:
        return [(direction, server_id)]
    return [
        (fallback, border.tiles[fallback])
        for fallback in DIAGONAL_FALLBACK.get(direction, ())
        if border.tiles.get(fallback)
    ]


def block_description(owner: GroundBrush | None, block: BorderBlock | None) -> dict[str, object] | None:
    if block is None:
        return None
    return {
        "ownerBrush": owner.name if owner else None,
        "border": block.border.key if block.border else None,
        "align": "outer" if block.outer else "inner",
        "to": block.to,
        "super": block.super_border,
        "specificCases": block.specific_count,
    }


def write_contact_sheet(
    rows: list[dict[str, object]],
    renderer: ItemRenderer,
    output: Path,
) -> None:
    if not rows:
        return
    columns = 4
    cell_w = 160
    cell_h = 48
    sheet = Image.new(
        "RGBA",
        (columns * cell_w, math.ceil(len(rows) / columns) * cell_h),
        (20, 20, 20, 255),
    )
    draw = ImageDraw.Draw(sheet)
    for index, row in enumerate(rows):
        x = (index % columns) * cell_w
        y = (index // columns) * cell_h
        image, client_id, sprite_ids = renderer.render(int(row["serverId"]), index, 0)
        sheet.alpha_composite(image, (x + 2, y + 2))
        label = f"{row['edge']} sid {row['serverId']} cid {client_id}"
        draw.text((x + 36, y + 5), label, fill=(235, 235, 235, 255))
        draw.text((x + 36, y + 20), ",".join(str(value) for value in sprite_ids), fill=(180, 180, 180, 255))
    sheet.save(output)


def collect_border_rows(blocks: list[tuple[GroundBrush, BorderBlock | None]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, int, int]] = set()
    for owner, block in blocks:
        if block is None or block.border is None:
            continue
        for direction, server_id in sorted(block.border.tiles.items()):
            key = (block.border.key, direction, server_id)
            if key in seen:
                continue
            seen.add(key)
            rows.append(
                {
                    "ownerBrush": owner.name,
                    "border": block.border.key,
                    "edge": TYPE_TO_EDGE[direction],
                    "direction": direction,
                    "serverId": server_id,
                }
            )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build a parallel border mosaic using the native RME 8-neighbour border lookup table."
    )
    parser.add_argument("--foreground-brush", required=True, help="RME ground brush used for the islands")
    parser.add_argument("--background-brush", required=True, help="RME ground brush used around the islands")
    parser.add_argument("--rme-root", type=Path, default=DEFAULT_RME_ROOT)
    parser.add_argument("--version", default="772")
    parser.add_argument("--dat", type=Path, default=DEFAULT_DAT)
    parser.add_argument("--spr", type=Path, default=DEFAULT_SPR)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--grid", type=int, default=24)
    parser.add_argument("--seed", type=int, default=772)
    parser.add_argument("--keep-existing", action="store_true")
    args = parser.parse_args()

    if args.grid < 16:
        raise SystemExit("--grid must be at least 16")

    data_dir = args.rme_root / "data" / args.version
    global_borders = load_global_borders(data_dir / "borders.xml")
    brushes = load_ground_brushes(data_dir / "grounds.xml", global_borders)
    missing = [
        name
        for name in (args.foreground_brush, args.background_brush)
        if name not in brushes
    ]
    if missing:
        raise SystemExit(f"RME ground brushes not found: {', '.join(missing)}")

    foreground = brushes[args.foreground_brush]
    background = brushes[args.background_brush]
    border_table = load_rme_border_table(args.rme_root / "source" / "brush_tables.cpp")
    forward_block = select_normal_border(background, foreground)
    reverse_block = select_normal_border(foreground, background)
    forward_owner = owner_for_block(forward_block, background, foreground)
    reverse_owner = owner_for_block(reverse_block, foreground, background)
    selected_blocks = [
        (owner, block)
        for owner, block in ((forward_owner, forward_block), (reverse_owner, reverse_block))
        if owner is not None
    ]

    warnings: list[str] = []
    for owner, block in selected_blocks:
        if block and block.specific_count:
            warnings.append(
                f"Border {block.border.key if block.border else 'none'} owned by {owner.name} "
                f"has {block.specific_count} specific case(s); base border is rendered without replacements"
            )
    if foreground.has_optional_border or background.has_optional_border:
        warnings.append("Optional borders are present in this pair and are not simulated")
    if foreground.friend_of(background) or background.friend_of(foreground):
        warnings.append("The selected brushes are friends/enemies; optional-border interaction is not simulated")

    folder_name = f"{args.foreground_brush}-over-{args.background_brush}".replace(" ", "-")
    out_dir = args.out_root / folder_name
    if out_dir.exists() and not args.keep_existing:
        shutil.rmtree(out_dir)
    source_dir = out_dir / "source-tiles"
    source_dir.mkdir(parents=True, exist_ok=True)

    server_to_client = read_otb_mapping(data_dir / "items.otb")
    server_ids = {
        server_id
        for brush in (foreground, background)
        for server_id, _chance in brush.items
    }
    border_rows = collect_border_rows(selected_blocks)
    server_ids.update(int(row["serverId"]) for row in border_rows)
    missing_mappings = sorted(server_id for server_id in server_ids if server_id not in server_to_client)
    if missing_mappings:
        raise SystemExit(f"Server ids missing from items.otb: {missing_mappings}")

    client_ids = {server_to_client[server_id] for server_id in server_ids}
    things = parse_dat(args.dat, int(args.version), THING_CATEGORY_ITEM, client_ids)
    renderer = ItemRenderer(server_to_client, things, args.spr, source_dir)
    field = build_brush_field(args.grid)
    rng = random.Random(args.seed)
    mosaic = Image.new("RGBA", (args.grid * 32, args.grid * 32), (0, 0, 0, 0))
    placements: list[dict[str, object]] = []
    mask_counts: Counter[int] = Counter()
    edge_counts: Counter[str] = Counter()

    for tile_y in range(args.grid):
        for tile_x in range(args.grid):
            current = foreground if field[tile_y][tile_x] else background
            other = background if current is foreground else foreground
            ground_server_id = choose_ground_item(current, rng)
            tile, ground_client_id, ground_sprite_ids = renderer.render(ground_server_id, tile_x, tile_y)
            mask = neighbour_mask(field, tile_x, tile_y)
            block = select_normal_border(current, other) if mask else None
            block_owner = owner_for_block(block, current, other)
            border_items = []
            if block and block.border:
                for direction in border_table[mask]:
                    for rendered_direction, border_server_id in expand_border_direction(block.border, direction):
                        border_image, border_client_id, border_sprite_ids = renderer.render(
                            border_server_id,
                            tile_x,
                            tile_y,
                        )
                        tile.alpha_composite(border_image)
                        edge = TYPE_TO_EDGE[rendered_direction]
                        edge_counts[edge] += 1
                        border_items.append(
                            {
                                "requestedDirection": direction,
                                "requestedEdge": TYPE_TO_EDGE[direction],
                                "direction": rendered_direction,
                                "edge": edge,
                                "serverId": border_server_id,
                                "clientId": border_client_id,
                                "spriteIds": border_sprite_ids,
                            }
                        )
            if mask:
                mask_counts[mask] += 1

            mosaic.alpha_composite(tile, (tile_x * 32, tile_y * 32))
            placements.append(
                {
                    "tileX": tile_x,
                    "tileY": tile_y,
                    "brush": current.name,
                    "otherBrush": other.name,
                    "groundServerId": ground_server_id,
                    "groundClientId": ground_client_id,
                    "groundSpriteIds": ground_sprite_ids,
                    "neighbourMask": mask,
                    "tableDirections": border_table[mask],
                    "borderBlock": block_description(block_owner, block),
                    "borderItems": border_items,
                }
            )

    mosaic_path = out_dir / f"{folder_name}-rme-native-mosaic-{args.grid}x{args.grid}-1x.png"
    mosaic.save(mosaic_path)

    atlas_receiver = background
    atlas_other = foreground
    atlas_block = select_normal_border(atlas_receiver, atlas_other)
    if atlas_block is None:
        atlas_receiver, atlas_other = foreground, background
        atlas_block = select_normal_border(atlas_receiver, atlas_other)

    atlas_path = out_dir / f"{folder_name}-rme-mask-atlas-16x16-1x.png"
    atlas = Image.new("RGBA", (16 * 32, 16 * 32), (0, 0, 0, 0))
    atlas_rows = []
    atlas_rng = random.Random(args.seed)
    for mask in range(256):
        x = mask % 16
        y = mask // 16
        ground_server_id = choose_ground_item(atlas_receiver, atlas_rng)
        tile, ground_client_id, ground_sprite_ids = renderer.render(ground_server_id, x, y)
        items = []
        if atlas_block and atlas_block.border:
            for direction in border_table[mask]:
                for rendered_direction, server_id in expand_border_direction(atlas_block.border, direction):
                    border_image, client_id, sprite_ids = renderer.render(server_id, x, y)
                    tile.alpha_composite(border_image)
                    items.append(
                        {
                            "edge": TYPE_TO_EDGE[rendered_direction],
                            "serverId": server_id,
                            "clientId": client_id,
                            "spriteIds": sprite_ids,
                        }
                    )
        atlas.alpha_composite(tile, (x * 32, y * 32))
        atlas_rows.append(
            {
                "mask": mask,
                "tileX": x,
                "tileY": y,
                "directions": border_table[mask],
                "groundServerId": ground_server_id,
                "groundClientId": ground_client_id,
                "groundSpriteIds": ground_sprite_ids,
                "borderItems": items,
            }
        )
    atlas.save(atlas_path)

    contact_path = out_dir / f"{folder_name}-border-contact-sheet.png"
    write_contact_sheet(border_rows, renderer, contact_path)

    manifest = {
        "type": "rme-native-border-mosaic-v1",
        "status": "partial" if warnings else "base-rme-compatible",
        "foregroundBrush": foreground.name,
        "backgroundBrush": background.name,
        "brushes": {
            foreground.name: {
                "zOrder": foreground.z_order,
                "items": [
                    {"serverId": server_id, "chance": chance}
                    for server_id, chance in foreground.items
                ],
            },
            background.name: {
                "zOrder": background.z_order,
                "items": [
                    {"serverId": server_id, "chance": chance}
                    for server_id, chance in background.items
                ],
            },
        },
        "pairSelection": {
            f"{background.name}->{foreground.name}": block_description(forward_owner, forward_block),
            f"{foreground.name}->{background.name}": block_description(reverse_owner, reverse_block),
        },
        "rmeBorderTable": {
            "source": str((args.rme_root / "source" / "brush_tables.cpp").resolve()),
            "entries": len(border_table),
            "neighbourBits": TILE_VALUES,
        },
        "tileSize": 32,
        "grid": {"width": args.grid, "height": args.grid},
        "seed": args.seed,
        "mosaic": str(mosaic_path.resolve()),
        "maskAtlas": str(atlas_path.resolve()),
        "contactSheet": str(contact_path.resolve()),
        "sourceTiles": str(source_dir.resolve()),
        "borderRows": border_rows,
        "maskCounts": {str(key): value for key, value in sorted(mask_counts.items())},
        "edgeCounts": dict(sorted(edge_counts.items())),
        "warnings": warnings,
        "limitations": [
            "Simulates one foreground/background brush pair at a time.",
            "Uses the exact 256-entry GroundBrush border lookup table from RME source.",
            "Uses RME z-order, inner/outer alignment and to-brush selection.",
            "Specific replacement rules and optional mountain borders are reported but not applied.",
            "Animation frame 0 and pattern Z 0 are used; pattern X/Y follows tile coordinates.",
        ],
        "placements": placements,
        "maskAtlasPlacements": atlas_rows,
    }
    manifest_path = out_dir / "_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(
        json.dumps(
            {
                "mosaic": str(mosaic_path.resolve()),
                "maskAtlas": str(atlas_path.resolve()),
                "manifest": str(manifest_path.resolve()),
                "sourceSprites": len(renderer.sprite_cache),
                "edgeCounts": dict(sorted(edge_counts.items())),
                "warnings": warnings,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
