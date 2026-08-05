import random
import sys
import xml.etree.ElementTree as ET
from collections import Counter

CHANCE_MAX = 100000  # TFS: 100000 = 100%

def simulate_loot(monster_xml, simulations):
    tree = ET.parse(monster_xml)
    root = tree.getroot()

    loot_node = root.find("loot")
    if loot_node is None:
        print("Esse monstro não tem loot.")
        return

    items = []

    for item in loot_node.findall("item"):
        item_id = item.get("id")
        name = item.get("name")
        chance_raw = item.get("chance", "0")
        countmax = int(item.get("countmax", "1"))

        try:
            chance = float(chance_raw)
        except ValueError:
            chance = 0

        items.append({
            "id": item_id,
            "name": name or item_id,
            "chance": chance,
            "countmax": countmax,
        })

    drops = Counter()

    for _ in range(simulations):
        for item in items:
            roll = random.uniform(0, CHANCE_MAX)
            if roll <= item["chance"]:
                drops[item["name"]] += 1

    print(f"\nSimulações: {simulations:,}".replace(",", "."))
    print("-" * 50)

    for item in items:
        total = drops[item["name"]]
        percent = (total / simulations) * 100
        configured = (item["chance"] / CHANCE_MAX) * 100

        print(
            f"{item['name']} | "
            f"chance configurada: {configured:.6f}% | "
            f"drops: {total} | "
            f"resultado: {percent:.6f}%"
        )


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Uso:")
        print("python tools/loot_test.py data/monster/rats/rat.xml 1000000")
        sys.exit(1)

    monster_xml = sys.argv[1]
    simulations = int(sys.argv[2])

    simulate_loot(monster_xml, simulations)