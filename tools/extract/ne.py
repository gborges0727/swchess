"""Read the resource table out of a 16-bit New Executable file.

The extractor never loads a DLL. It reads the MZ header, follows e_lfanew to the
NE header, then walks the resource table at NE offset 0x24. Type and name ids
are either a number with bit 15 set or a byte offset to a Pascal string inside
the resource table.
"""

import struct

RT_BITMAP = 2
RT_STRING = 6


class Resource:
    """One entry in the NE resource table."""

    def __init__(self, type_id, name_id, offset, length, flags):
        self.type_id = type_id
        self.name_id = name_id
        self.offset = offset
        self.length = length
        self.flags = flags

    def data(self, blob):
        return blob[self.offset : self.offset + self.length]

    def as_dict(self):
        return {
            "type": self.type_id,
            "name": self.name_id,
            "offset": self.offset,
            "length": self.length,
            "flags": self.flags,
        }

    def __repr__(self):
        return f"Resource(type={self.type_id!r}, name={self.name_id!r}, offset={self.offset}, length={self.length})"


def read_resources(blob):
    """Return every resource in an NE image, with file offsets already shifted."""
    if blob[:2] != b"MZ":
        raise ValueError("not an MZ image")
    ne_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[ne_off : ne_off + 2] != b"NE":
        raise ValueError("no NE header")
    table = ne_off + struct.unpack_from("<H", blob, ne_off + 0x24)[0]
    shift = struct.unpack_from("<H", blob, table)[0]

    def resolve(value):
        if value & 0x8000:
            return value & 0x7FFF
        start = table + value
        length = blob[start]
        return blob[start + 1 : start + 1 + length].decode("latin-1")

    pos = table + 2
    out = []
    while True:
        type_value = struct.unpack_from("<H", blob, pos)[0]
        if type_value == 0:
            break
        count = struct.unpack_from("<H", blob, pos + 2)[0]
        pos += 8
        for _ in range(count):
            offset, length, flags, name_value = struct.unpack_from("<HHHH", blob, pos)
            pos += 12
            out.append(
                Resource(
                    resolve(type_value),
                    resolve(name_value),
                    offset << shift,
                    length << shift,
                    flags,
                )
            )
    return out


def read_file(path):
    """Read one NE file and return its bytes plus its resource list."""
    with open(path, "rb") as fh:
        blob = fh.read()
    return blob, read_resources(blob)
