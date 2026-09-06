"""Write tools/fonts/strings.csv: one row per string id, one column per language.

It reads the JSON the asset extractor already wrote under assets/locales, turns
each string's raw bytes into the text a player sees, and lines the four
languages up by string id. Which byte means which character depends on the font
that draws the string, so the id decides the mapping: ids 14000 to 14031 (the
opening crawl) and 14992 to 15199 (the credit roll) go through LEGFONT, and
every other id goes through GUITEXT. Ids 14000 and 15000 hold line counts rather
than text, so they stay as written.

    python3 -m tools.fonts.strings_csv --locales assets/locales --out tools/fonts/strings.csv
"""

import argparse
import csv
import json
import os

from .legfont import GUITEXT_MAP, LEGFONT_MAP

LANGUAGES = [("eng", "english"), ("frn", "french"), ("ger", "german"), ("spn", "spanish")]
LEGFONT_IDS = [(14000, 14031), (14992, 15199)]
RAW_IDS = {14000, 15000}


def font_for(string_id):
    """Return which font draws this string id."""
    for low, high in LEGFONT_IDS:
        if low <= string_id <= high:
            return "legfont"
    return "guitext"


def decode(string_id, raw):
    """Turn one string's bytes into the text a player sees."""
    if string_id in RAW_IDS:
        return raw
    table = LEGFONT_MAP if font_for(string_id) == "legfont" else GUITEXT_MAP
    return "".join(table.get(ch, ch) for ch in raw)


def read_language(path):
    """Return {string_id: raw text or None} for one locale JSON file."""
    with open(path) as fh:
        doc = json.load(fh)
    out = {}
    for table in doc["string_tables"]:
        for entry in table["strings"]:
            if not entry["present"]:
                out[entry["string_id"]] = None
            else:
                out[entry["string_id"]] = bytes.fromhex(entry["bytes_hex"]).decode("latin-1")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--locales", default="assets/locales")
    parser.add_argument("--out", default="tools/fonts/strings.csv")
    args = parser.parse_args()

    langs = {short: read_language(os.path.join(args.locales, f"{name}.json")) for short, name in LANGUAGES}
    ids = sorted(langs["eng"])
    for short, table in langs.items():
        if sorted(table) != ids:
            raise ValueError(f"{short} does not carry the same string ids as eng")

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["id", "eng", "frn", "ger", "spn"])
        for string_id in ids:
            row = [string_id]
            for short, _ in LANGUAGES:
                raw = langs[short][string_id]
                row.append("" if raw is None else decode(string_id, raw))
            writer.writerow(row)
    print(f"wrote {len(ids)} rows to {args.out}")


if __name__ == "__main__":
    main()
