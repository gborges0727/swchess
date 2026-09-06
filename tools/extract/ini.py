"""Read the game's INI files and keep every field exactly as written.

The Windows profile reader that the original game calls ignores case in section
and key names, so this reader does the same. It keeps the original spelling of
each section and key alongside the lookup form, and it keeps the values as raw
strings. Nothing here converts a value to a number.
"""


class Section:
    """One INI section with its keys in file order."""

    def __init__(self, name):
        self.name = name
        self.keys = []
        self._by_lower = {}

    def add(self, key, value):
        self.keys.append(key)
        self._by_lower[key.lower()] = value

    def get(self, key, default=None):
        return self._by_lower.get(key.lower(), default)

    def raw(self):
        """Return the section as an ordinary dict in file order."""
        return {k: self._by_lower[k.lower()] for k in self.keys}

    def __contains__(self, key):
        return key.lower() in self._by_lower

    def __len__(self):
        return len(self.keys)


class IniFile:
    """A parsed INI file. Section lookup ignores case."""

    def __init__(self, path):
        self.path = path
        self.sections = []
        self._by_lower = {}
        self._parse()

    def _parse(self):
        with open(self.path, "rb") as fh:
            text = fh.read().decode("cp437")
        current = None
        for line in text.splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith(";"):
                continue
            if stripped.startswith("["):
                end = stripped.find("]")
                if end < 0:
                    continue
                current = Section(stripped[1:end].strip())
                self.sections.append(current)
                self._by_lower[current.name.lower()] = current
                continue
            if current is None or "=" not in stripped:
                continue
            key, _, value = stripped.partition("=")
            current.add(key.strip(), value.strip())

    def section(self, name):
        return self._by_lower.get(name.lower())

    def names(self):
        return [s.name for s in self.sections]


def as_int(value, default=None):
    """Turn an INI value into an int, or return default when it is not one."""
    if value is None:
        return default
    try:
        return int(value.strip())
    except (ValueError, AttributeError):
        return default
