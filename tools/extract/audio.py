"""Pull the sound effects out of SWCAUDIO.DLL and copy the four loose WAV files.

SWCAUDIO.DLL holds 110 resources of type WAVE. The first RIFF starts at file
offset 0x1400. The resource table rounds every length up to the 512-byte
alignment, so the real size comes from the RIFF header instead. Two records are
both named GRUNT1.WAV, which leaves 109 distinct names.
"""

import hashlib
import os
import shutil
import struct

from . import ne

STANDALONE = ["BLKVIC.WAV", "STWPRES.WAV", "SWTHEME.WAV", "WHTVIC.WAV"]


def riff_length(blob, start):
    """Return the true byte length of the RIFF at `start`."""
    if blob[start : start + 4] != b"RIFF":
        raise ValueError(f"no RIFF at {start:#x}")
    return 8 + struct.unpack_from("<I", blob, start + 4)[0]


def describe_wave(data):
    """Read the fmt chunk and the data chunk size out of one RIFF in memory."""
    info = {
        "channels": None,
        "sample_rate": None,
        "bits_per_sample": None,
        "format_tag": None,
        "data_bytes": None,
        "duration_seconds": None,
    }
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return info
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos : pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if tag == b"fmt " and size >= 16:
            fmt, ch, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", data, body)
            info.update(
                format_tag=fmt, channels=ch, sample_rate=rate, bits_per_sample=bits
            )
        elif tag == b"data":
            info["data_bytes"] = size
        pos = body + size + (size & 1)
    if info["data_bytes"] and info["sample_rate"] and info["channels"] and info["bits_per_sample"]:
        frame = info["channels"] * info["bits_per_sample"] // 8
        if frame:
            info["duration_seconds"] = round(info["data_bytes"] / frame / info["sample_rate"], 6)
    return info


def extract(cd_dir, out_dir):
    """Write every sound into out_dir and return the catalog entries."""
    os.makedirs(out_dir, exist_ok=True)
    dll_path = os.path.join(cd_dir, "SWCAUDIO.DLL")
    blob, resources = ne.read_file(dll_path)
    waves = [r for r in resources if r.type_id == "WAVE"]

    records = []
    written = {}
    duplicates = []
    for res in waves:
        length = riff_length(blob, res.offset)
        data = blob[res.offset : res.offset + length]
        digest = hashlib.sha256(data).hexdigest()
        name = res.name_id
        entry = {
            "source": "SWCAUDIO.DLL",
            "resource_name": name,
            "resource_offset": res.offset,
            "resource_length": res.length,
            "riff_length": length,
            "sha256": digest,
        }
        entry.update(describe_wave(data))
        if name in written:
            entry["duplicate_of"] = written[name]["output"]
            entry["identical_bytes"] = written[name]["sha256"] == digest
            entry["output"] = None
            duplicates.append(name)
            if not entry["identical_bytes"]:
                alt = f"{os.path.splitext(name)[0]}__dup2.WAV"
                with open(os.path.join(out_dir, alt), "wb") as fh:
                    fh.write(data)
                entry["output"] = f"audio/{alt}"
        else:
            with open(os.path.join(out_dir, name), "wb") as fh:
                fh.write(data)
            entry["output"] = f"audio/{name}"
            written[name] = entry
        records.append(entry)

    standalone = []
    for name in STANDALONE:
        src = os.path.join(cd_dir, name)
        shutil.copyfile(src, os.path.join(out_dir, name))
        with open(src, "rb") as fh:
            data = fh.read()
        entry = {
            "source": name,
            "resource_name": name,
            "resource_offset": 0,
            "resource_length": len(data),
            "riff_length": riff_length(data, 0) if data[:4] == b"RIFF" else len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "output": f"audio/{name}",
        }
        entry.update(describe_wave(data))
        standalone.append(entry)

    index = {name.upper() for name in written}
    index.update(name.upper() for name in STANDALONE)
    return {
        "resource_records": records,
        "standalone_files": standalone,
        "distinct_resource_names": len(written),
        "resource_record_count": len(records),
        "duplicate_resource_names": sorted(set(duplicates)),
    }, index
