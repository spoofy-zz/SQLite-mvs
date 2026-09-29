#!/usr/bin/env python3
"""Shorten cc370 private labels that overflow HLASM's 8-character limit."""

from pathlib import Path
import re
import sys


REPLACEMENTS = (
    (re.compile(r"@@FEN([0-9]+)"), r"@E\1"),
    (re.compile(r"@@PGT([0-9]+)"), r"@P\1"),
    (re.compile(r"@@PGE([0-9]+)"), r"@G\1"),
)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT.s OUTPUT.s")
    source = Path(sys.argv[1]).read_text(encoding="ascii")
    for pattern, replacement in REPLACEMENTS:
        source = pattern.sub(replacement, source)
    Path(sys.argv[2]).write_text(source, encoding="ascii")


if __name__ == "__main__":
    main()

