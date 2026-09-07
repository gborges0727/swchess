"""Find a CD file by name without matching case.

The original CD writes every filename in uppercase, such as CM.INI and
SWCAUDIO.DLL. A copy made on macOS or Windows can lowercase the names and the
bytes stay the same. Linux then fails to open the uppercase name.

cd_path indexes the directory once by the uppercased form of every entry, so
the extractor can ask for the uppercase name and get whatever the copy calls
it. Two entries that differ only by case raise an error, because then no single
answer is right. Nothing on the player's disk is renamed.

The C++ readers do the same thing in src/assets/cdfs.h.
"""

import os

_indexes = {}


def _index(cd_dir):
    key = os.path.abspath(cd_dir)
    found = _indexes.get(key)
    if found is None:
        by_upper = {}
        ambiguous = {}
        for name in os.listdir(cd_dir):
            upper = name.upper()
            if upper in by_upper and by_upper[upper] != name:
                ambiguous[upper] = (by_upper[upper], name)
            else:
                by_upper[upper] = name
        found = (by_upper, ambiguous)
        _indexes[key] = found
    return found


def cd_name(cd_dir, name):
    """Return the real filename for a name written in any case, or None."""
    by_upper, ambiguous = _index(cd_dir)
    upper = name.upper()
    if upper in ambiguous:
        first, second = ambiguous[upper]
        raise RuntimeError(
            f"{cd_dir} holds {first} and {second}, two names that differ only "
            f"by case, so {name} has no single match"
        )
    return by_upper.get(upper)


def cd_path(cd_dir, name):
    """Return the path of one CD file. Raises when the directory has no match."""
    real = cd_name(cd_dir, name)
    if real is None:
        raise FileNotFoundError(f"{cd_dir} has no {name}")
    return os.path.join(cd_dir, real)


def cd_exists(cd_dir, name):
    """Say whether the directory holds the file under any spelling."""
    return cd_name(cd_dir, name) is not None


def cd_glob(cd_dir, extension):
    """Return every path whose extension matches, ignoring case.

    `extension` includes the dot, as in '.ANX'. The result is sorted by the
    uppercased filename, so a lowercased copy lists in the same order as the
    original CD.
    """
    want = extension.upper()
    by_upper, _ = _index(cd_dir)
    return [
        os.path.join(cd_dir, by_upper[upper])
        for upper in sorted(by_upper)
        if upper.endswith(want)
    ]
