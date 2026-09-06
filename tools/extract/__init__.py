"""Reference asset extractor for the Star Wars Chess native port.

Run it as a module:

    python3 -m tools.extract --cd original/win3x/cd --out assets

It reads the original CD files without changing them and writes PNGs, WAVs and
JSON manifests into the output directory. The layout follows section 2 of
docs/plan.md.
"""

VERSION = "1.0.0"
