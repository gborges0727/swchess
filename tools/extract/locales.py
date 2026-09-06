"""Save the string tables from the four language DLLs without guessing at text.

A 16-bit Windows string table resource holds sixteen strings. Each one starts
with a length byte and runs for that many bytes. The string id is
(resource id - 1) * 16 + position, and an empty entry is a real entry the game
uses, so nothing is dropped.

The bytes are not ordinary Windows text. RESFRN.DLL holds "tr2s" and RESGER.DLL
holds "K\\NIG", which means the game substitutes glyphs while drawing. So each
entry keeps the original bytes as hex, and adds a cp437 and a latin-1 reading as
a convenience. Neither reading is the display text, and no character is replaced
anywhere in this module.
"""

import hashlib
import json
import os

from . import ne

LANGUAGES = {
    "RESENG.DLL": "english",
    "RESFRN.DLL": "french",
    "RESGER.DLL": "german",
    "RESSPN.DLL": "spanish",
}


def split_string_table(data):
    """Split one string table resource into its sixteen length-prefixed entries."""
    out = []
    pos = 0
    for index in range(16):
        if pos >= len(data):
            out.append({"index": index, "length": None, "present": False})
            continue
        length = data[pos]
        body = data[pos + 1 : pos + 1 + length]
        pos += 1 + length
        out.append(
            {
                "index": index,
                "length": length,
                "present": True,
                "bytes_hex": body.hex(),
                "cp437": body.decode("cp437", "replace"),
                "latin1": body.decode("latin-1", "replace"),
            }
        )
    return out, pos


def extract(cd_dir, out_dir):
    """Write one JSON file per language and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    files = []
    for filename, language in LANGUAGES.items():
        path = os.path.join(cd_dir, filename)
        blob, resources = ne.read_file(path)
        tables = []
        string_count = 0
        for res in resources:
            if res.type_id != ne.RT_STRING:
                continue
            data = res.data(blob)
            strings, consumed = split_string_table(data)
            base = (int(res.name_id) - 1) * 16
            for entry in strings:
                entry["string_id"] = base + entry["index"]
                if entry["present"]:
                    string_count += 1
            tables.append(
                {
                    "resource_id": res.name_id,
                    "resource_offset": res.offset,
                    "resource_offset_hex": f"{res.offset:#x}",
                    "resource_length": res.length,
                    "bytes_consumed": consumed,
                    "trailing_bytes_hex": data[consumed:].rstrip(b"\x00").hex(),
                    "strings": strings,
                }
            )
        other = [r.as_dict() for r in resources if r.type_id != ne.RT_STRING]
        document = {
            "language": language,
            "source": filename,
            "source_sha256": hashlib.sha256(blob).hexdigest(),
            "encoding_note": "the bytes are not plain Windows text, the game substitutes glyphs while drawing",
            "string_table_count": len(tables),
            "string_count": string_count,
            "other_resources": other,
            "string_tables": tables,
        }
        out = os.path.join(out_dir, language + ".json")
        with open(out, "w") as fh:
            json.dump(document, fh, indent=1, ensure_ascii=False)
        files.append(
            {
                "language": language,
                "source": filename,
                "string_table_count": len(tables),
                "string_count": string_count,
                "output": f"locales/{language}.json",
            }
        )
    return {"locales": files, "locale_file_count": len(files)}
