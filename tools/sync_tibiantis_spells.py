from __future__ import annotations

import html
import re
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


SPELLS_URL = "https://tibiantis.info/library/spells"
SPELLS_XML = Path(r"C:\tibia-oldschool\server\data\spells\spells.xml")
CONJ_DIR = Path(r"C:\tibia-oldschool\server\data\spells\scripts\conjuring")
REPORT_PATH = Path(r"C:\tibia-oldschool\tools\tibiantis_sync_report.txt")

SYSTEM_WORDS = {
    "aleta grav",
    "aleta sio",
    "alana sio",
    "aleta som",
    "adori blank",
}


def fetch_html() -> str:
    req = urllib.request.Request(
        SPELLS_URL,
        headers={
            "User-Agent": "Mozilla/5.0",
            "Accept": "text/html,application/xhtml+xml",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as response:
        return response.read().decode("utf-8", "ignore")


def clean_name(name_html: str) -> str:
    text = re.sub(r"<.*?>", "", name_html)
    return html.unescape(text).strip()


def extract_words(name_html: str) -> str:
    match = re.search(r"<small>\((.*?)</small>\)", name_html)
    if not match:
        return clean_name(name_html)
    return html.unescape(match.group(1)).strip()


def parse_entries(page_html: str) -> list[dict[str, str]]:
    match = re.search(r"var entries = \[(.*?)]\s*;\s*fillSorted", page_html, re.S)
    if not match:
        raise RuntimeError("Could not locate Tibiantis entries array.")

    array_text = match.group(1)
    blocks = re.findall(r"\{(.*?)\}", array_text, re.S)
    entries: list[dict[str, str]] = []

    for block in blocks:
        data: dict[str, str] = {}
        for key in ("name", "vocation", "group", "type", "premium", "charges", "usage"):
            m = re.search(rf"{key}:\s*\"(.*?)\"", block, re.S)
            data[key] = m.group(1).strip() if m else ""

        for key in ("mlvl", "mana", "price"):
            m = re.search(rf"{key}:\s*([0-9]+)", block)
            data[key] = m.group(1).strip() if m else ""

        data["display_name"] = clean_name(data["name"])
        data["words"] = extract_words(data["name"])
        entries.append(data)

    return entries


def normalize_name(name: str) -> str:
    stripped = re.sub(r"\(.*?\)", "", name)
    return re.sub(r"\s+", " ", stripped.replace(" Rune", "")).strip().lower()


def build_site_maps(entries: list[dict[str, str]]):
    by_words: dict[str, dict[str, str]] = {}
    by_name: dict[str, dict[str, str]] = {}
    for entry in entries:
        by_words[entry["words"].lower()] = entry
        by_name[normalize_name(entry["display_name"])] = entry
    return by_words, by_name


def find_site_entry(local_name: str, local_words: str, by_words, by_name):
    words_key = local_words.lower()
    if words_key in by_words:
        return by_words[words_key]

    name_match = by_name.get(normalize_name(local_name))
    if name_match:
        return name_match

    # Some entries like summon creature carry parameter hints on the fansite.
    for site_words, entry in by_words.items():
        if site_words.startswith(words_key) or words_key.startswith(site_words):
            return entry

    return None


def premium_value(value: str) -> str:
    return "1" if value.strip().lower() == "yes" else "0"


def ensure_attr(elem: ET.Element, preferred: str, fallback: str | None = None):
    if preferred in elem.attrib:
        return preferred
    if fallback and fallback in elem.attrib:
        return fallback
    elem.set(preferred, "")
    return preferred


def sync_xml(entries: list[dict[str, str]]):
    by_words, by_name = build_site_maps(entries)
    tree = ET.parse(SPELLS_XML)
    root = tree.getroot()

    matched_local_words: set[str] = set()
    unmatched_local: list[str] = []

    for elem in root:
        if elem.tag not in ("instant", "rune"):
            continue

        local_name = elem.attrib.get("name", "")
        local_words = elem.attrib.get("words", local_name)
        if local_words in SYSTEM_WORDS:
            continue

        site_entry = find_site_entry(local_name, local_words, by_words, by_name)
        if not site_entry:
            unmatched_local.append(f"{elem.tag}: {local_name} ({local_words})")
            continue

        matched_local_words.add(site_entry["words"].lower())

        if elem.tag == "instant":
            if site_entry["mana"]:
                elem.set("mana", site_entry["mana"])

            level_attr = "lvl" if "lvl" in elem.attrib else "level"
            elem.set(level_attr, "0")
            elem.set("maglv", site_entry["mlvl"] or "0")

            prem_attr = "prem" if "prem" in elem.attrib else ("premium" if "premium" in elem.attrib else "premium")
            elem.set(prem_attr, premium_value(site_entry["premium"]))

        elif elem.tag == "rune":
            if site_entry["usage"].isdigit():
                elem.set("maglv", site_entry["usage"])
            if site_entry["charges"].isdigit():
                elem.set("charges", site_entry["charges"])

    ET.indent(tree, space="\t", level=0)
    tree.write(SPELLS_XML, encoding="UTF-8", xml_declaration=True)

    unmatched_site = []
    for entry in entries:
        if entry["words"].lower() not in matched_local_words:
            unmatched_site.append(f"{entry['type']}: {entry['display_name']} ({entry['words']})")

    return unmatched_local, unmatched_site


def sync_conjuring_counts(entries: list[dict[str, str]]):
    by_words, _ = build_site_maps(entries)
    conjured_updates: list[str] = []
    unchanged: list[str] = []

    for path in sorted(CONJ_DIR.glob("*.lua")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        match = re.search(r"conjureItem\((\d+),\s*(\d+),\s*(\d+)\)", text)
        if not match:
            unchanged.append(path.name)
            continue

        # resolve by spell words from xml script mapping later by filename convention
        spell_words = None
        lua_name = path.name
        with SPELLS_XML.open("r", encoding="utf-8", errors="ignore") as handle:
            xml_text = handle.read()
        script_match = re.search(
            rf"<instant[^>]*words=\"([^\"]+)\"[^>]*script=\"conjuring/{re.escape(lua_name)}\"",
            xml_text,
        )
        if script_match:
            spell_words = script_match.group(1).strip().lower()

        if not spell_words or spell_words not in by_words:
            unchanged.append(path.name)
            continue

        entry = by_words[spell_words]
        if entry["type"].lower() != "rune" or not entry["charges"].isdigit():
            unchanged.append(path.name)
            continue

        new_count = entry["charges"]
        new_text = re.sub(
            r"conjureItem\((\d+),\s*(\d+),\s*(\d+)\)",
            rf"conjureItem(\1, \2, {new_count})",
            text,
            count=1,
        )
        if new_text != text:
            path.write_text(new_text, encoding="utf-8")
            conjured_updates.append(f"{path.name} -> {new_count}")
        else:
            unchanged.append(path.name)

    return conjured_updates, unchanged


def write_report(unmatched_local, unmatched_site, conjured_updates):
    lines = []
    lines.append("Tibiantis spell sync report")
    lines.append("")
    lines.append("Local entries without Tibiantis match:")
    lines.extend(f"- {entry}" for entry in unmatched_local or ["(none)"])
    lines.append("")
    lines.append("Tibiantis entries without local match:")
    lines.extend(f"- {entry}" for entry in unmatched_site or ["(none)"])
    lines.append("")
    lines.append("Conjuring scripts updated:")
    lines.extend(f"- {entry}" for entry in conjured_updates or ["(none)"])
    REPORT_PATH.write_text("\n".join(lines), encoding="utf-8")


def main():
    html_text = fetch_html()
    entries = parse_entries(html_text)
    unmatched_local, unmatched_site = sync_xml(entries)
    conjured_updates, _ = sync_conjuring_counts(entries)
    write_report(unmatched_local, unmatched_site, conjured_updates)

    print(f"Parsed Tibiantis entries: {len(entries)}")
    print(f"Updated conjuring scripts: {len(conjured_updates)}")
    print(f"Report: {REPORT_PATH}")


if __name__ == "__main__":
    main()
