#!/usr/bin/env python3
"""Create an ASCII SQL import and an MVS FB320/EBCDIC copy from Chinook."""

import argparse
import sqlite3
import unicodedata
from pathlib import Path


TABLES = (
    "albums", "artists", "customers", "employees", "genres",
    "invoice_items", "invoices", "media_types", "playlist_track",
    "playlists", "tracks",
)

DROP_ORDER = (
    "invoice_items", "playlist_track", "invoices", "customers", "employees",
    "tracks", "albums", "playlists", "artists", "genres", "media_types",
)

TRANSLATE = str.maketrans({
    "Æ": "AE", "æ": "ae", "Ø": "O", "ø": "o", "Ð": "D", "ð": "d",
    "Þ": "Th", "þ": "th", "Ł": "L", "ł": "l", "ß": "ss",
    "‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-",
    "…": "...", "×": "x",
})


def ascii_text(value: str) -> str:
    value = value.translate(TRANSLATE)
    return unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")


def literal(value) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bytes):
        return "X'" + value.hex().upper() + "'"
    if isinstance(value, (int, float)):
        return repr(value)
    return "'" + ascii_text(str(value)).replace("'", "''") + "'"


def build(source: Path) -> list[str]:
    db = sqlite3.connect(f"file:{source}?mode=ro", uri=True)
    lines = ["PRAGMA foreign_keys=OFF;", "BEGIN IMMEDIATE;"]
    for table in DROP_ORDER:
        lines.append(f'DROP TABLE IF EXISTS "{table}";')
    for table in TABLES:
        row = db.execute(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name=?", (table,)
        ).fetchone()
        if not row or not row[0]:
            raise RuntimeError(f"missing Chinook table: {table}")
        lines.extend(ascii_text(row[0]).splitlines())
        lines[-1] += ";"
        query = f'SELECT * FROM "{table}"'
        for values in db.execute(query):
            data = ",".join(literal(value) for value in values)
            lines.append(f'INSERT INTO "{table}" VALUES({data});')
    for (sql,) in db.execute(
        "SELECT sql FROM sqlite_master WHERE type='index' AND sql IS NOT NULL "
        "AND tbl_name IN (%s) ORDER BY name" % ",".join("?" * len(TABLES)),
        TABLES,
    ):
        lines.extend(ascii_text(sql).splitlines())
        lines[-1] += ";"
    lines.append(
        'CREATE TABLE "__chinook_import_verify"('
        'value INTEGER NOT NULL CHECK(value=1));'
    )
    for table in TABLES:
        expected = db.execute(f'SELECT count(*) FROM "{table}"').fetchone()[0]
        lines.append(
            'INSERT INTO "__chinook_import_verify" '
            f'SELECT count(*)={expected} FROM "{table}";'
        )
    lines.extend((
        'INSERT INTO "__chinook_import_verify" SELECT count(*)=20 FROM people;',
        'INSERT INTO "__chinook_import_verify" SELECT count(*)=60 FROM orders;',
        'DROP TABLE "__chinook_import_verify";',
    ))
    lines.extend(("COMMIT;", "PRAGMA foreign_keys=ON;", "ANALYZE;"))
    db.close()
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("sql_output", type=Path)
    parser.add_argument("fb_output", type=Path)
    args = parser.parse_args()
    lines = build(args.source)
    if any(len(line) > 320 for line in lines):
        longest = max(len(line) for line in lines)
        raise RuntimeError(f"line length {longest} exceeds FB320")
    args.sql_output.write_text("\n".join(lines) + "\n", encoding="ascii")
    with args.fb_output.open("wb") as output:
        for line in lines:
            output.write(line.encode("cp037").ljust(320, b"\x40"))
    print(f"generated {len(lines)} FB320 records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
