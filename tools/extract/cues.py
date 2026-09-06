"""Match a wav= line in a capture INI to a real sound name.

FUN_1058_0c4d in XCHESS.EXE uppercases the INI value with AnsiUpper and then
calls FindResource on SWCAUDIO.DLL with that exact string. There is no path
stripping, no extension repair, and no file fallback. A value that does not name
a WAVE resource plays nothing, so this module resolves the same way and marks
every miss silent_in_original.
"""


def normalize(raw):
    """Upper case the value the way AnsiUpper does, and nothing else."""
    return raw.strip().upper()


def resolve(raw, index, capture, ini_file, section):
    """Resolve one wav= value against the WAVE resource names.

    `index` maps each upper case sound name to its length in milliseconds. The
    returned entry always says which of resolved or silent_in_original applies,
    so nothing is dropped silently.
    """
    plain = normalize(raw)
    entry = {
        "raw": raw,
        "normalized": plain,
        "resolved": None,
        "duration_ms": None,
        "status": "silent_in_original",
        "capture": capture,
        "ini_file": ini_file,
        "section": section,
    }

    if plain in index:
        entry["resolved"] = plain
        entry["duration_ms"] = index[plain]
        entry["status"] = "resolved"
        return entry

    entry["silent_reason"] = (
        f"SWCAUDIO.DLL holds no WAVE resource named {plain}, so the original "
        "plays nothing on this frame"
    )
    return entry
