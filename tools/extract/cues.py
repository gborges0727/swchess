"""Match a wav= line in a capture INI to a real sound file.

The INI values are lower case and a few are mistyped. Matching ignores case.
Two values need repair before they match a resource name, and one names a file
that the disc does not contain.
"""

import ntpath

# The two mistyped cues the plan calls out, keyed by the raw INI value.
KNOWN_ALIASES = {
    "atftstep.awv": "ATFTSTEP.WAV",
    "r2alarm\\.wav": "R2ALARM.WAV",
}


def normalize(raw):
    """Strip any path, drop stray backslashes, and upper case the file name."""
    text = raw.strip().replace("/", "\\")
    text = ntpath.basename(text)
    text = text.replace("\\", "")
    return text.upper()


def resolve(raw, index, capture, ini_file, section):
    """Resolve one wav= value against the set of known sound names.

    `index` holds every sound name in upper case. The return value always says
    which of resolved, alias or missing applies, so nothing is dropped silently.
    """
    plain = normalize(raw)
    entry = {
        "raw": raw,
        "normalized": plain,
        "resolved": None,
        "status": "missing",
        "capture": capture,
        "ini_file": ini_file,
        "section": section,
    }

    if plain in index:
        entry["resolved"] = plain
        entry["status"] = "resolved"
        return entry

    alias = KNOWN_ALIASES.get(raw.strip().lower())
    if alias and alias in index:
        entry["resolved"] = alias
        entry["status"] = "alias"
        entry["alias_reason"] = "listed in docs/plan.md section 2 as an explicit cue alias"
        return entry

    stem = plain.rsplit(".", 1)[0]
    guess = stem + ".WAV"
    if guess in index:
        entry["resolved"] = guess
        entry["status"] = "alias"
        entry["alias_reason"] = "the extension differs from the resource name"
        return entry

    entry["missing_reason"] = "no WAVE resource and no standalone WAV carries this name"
    return entry
