from pathlib import Path
import re
import xml.etree.ElementTree as ET


ROOT = Path(r"C:\tibia-oldschool\server\data\spells")


def print_local_spells():
    spells_xml = ROOT / "spells.xml"
    root = ET.parse(spells_xml).getroot()
    print("=== LOCAL SPELLS ===")
    for elem in root:
        if elem.tag not in ("instant", "rune"):
            continue
        name = elem.attrib.get("name", "")
        words = elem.attrib.get("words", "")
        level = elem.attrib.get("lvl") or elem.attrib.get("level") or ""
        mana = elem.attrib.get("mana", "")
        maglv = elem.attrib.get("maglv", "")
        script = elem.attrib.get("script", "")
        print("\t".join([elem.tag, name, words, level, mana, maglv, script]))


def print_conjuring_counts():
    conj_dir = ROOT / "scripts" / "conjuring"
    print("=== CONJURING SCRIPTS ===")
    conjure_re = re.compile(r"conjureItem\((\d+),\s*(\d+),\s*(\d+)\)")
    create_re = re.compile(r"createItem\((\d+),\s*(\d+)\)")

    for path in sorted(conj_dir.glob("*.lua")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        conjure = conjure_re.search(text)
        create = create_re.search(text)
        if conjure:
            print(f"{path.name}\tblank={conjure.group(1)}\titem={conjure.group(2)}\tcount={conjure.group(3)}")
        elif create:
            print(f"{path.name}\tcreateItem item={create.group(1)}\tcount={create.group(2)}")
        else:
            print(f"{path.name}\t(no conjureItem)")


if __name__ == "__main__":
    print_local_spells()
    print_conjuring_counts()
