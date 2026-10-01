#!/usr/bin/env python3
"""Translate a small embedded-SQL subset into SQLite-mvs SQLITEX calls."""

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HOST_RE = re.compile(r":([A-Za-z][A-Za-z0-9-]*)")
PIC_RE = re.compile(
    r"^\s*\d+\s+([A-Za-z][A-Za-z0-9-]*)\s+.*?\bPIC\s+"
    r"([SX9V()+\-0-9]+)(?:\s+COMP(?:-[0-9])?)?\s*(?:VALUE\b.*)?\.\s*$",
    re.IGNORECASE,
)


@dataclass
class Host:
    name: str
    kind: str
    length: int


@dataclass
class SqlBlock:
    number: int
    sql: str
    inputs: list[str]
    outputs: list[str]

    @property
    def constant(self) -> str:
        return f"SQLE-SQL-{self.number:04d}"


def picture_length(picture: str) -> tuple[str, int]:
    value = picture.upper()
    kind = "T" if "X" in value else "I"
    match = re.search(r"[X9]\((\d+)\)", value)
    if match:
        return kind, int(match.group(1))
    if kind == "T":
        return kind, value.count("X") or 1
    return kind, value.count("9") or 1


def find_hosts(lines: list[str]) -> dict[str, Host]:
    hosts: dict[str, Host] = {}
    for line in lines:
        match = PIC_RE.match(line[6:] if len(line) > 6 else line)
        if not match:
            continue
        name = match.group(1).upper()
        kind, length = picture_length(match.group(2))
        hosts[name] = Host(name, kind, length)
    return hosts


def normalize_sql(text: str) -> str:
    sql = " ".join(text.replace("\n", " ").split())
    if sql.endswith("."):
        sql = sql[:-1].rstrip()
    return sql


def parse_statement(number: int, text: str) -> SqlBlock:
    sql = normalize_sql(text)
    outputs: list[str] = []
    if re.match(r"^SELECT\b", sql, re.IGNORECASE):
        select = re.match(
            r"^(SELECT\s+.+?)\s+INTO\s+(.+?)\s+FROM\s+(.+)$",
            sql,
            re.IGNORECASE,
        )
        if not select:
            raise ValueError("SELECT requires INTO host variables")
        output_text = select.group(2)
        outputs = [part.strip()[1:].upper() for part in output_text.split(",")]
        if any(not part.strip().startswith(":")
               for part in output_text.split(",")):
            raise ValueError("every SELECT INTO target must be a host variable")
        sql = f"{select.group(1)} FROM {select.group(3)}"
    elif not re.match(r"^(INSERT|UPDATE|DELETE)\b", sql, re.IGNORECASE):
        raise ValueError("supported statements are SELECT INTO, INSERT, UPDATE, DELETE")
    inputs = [match.group(1).upper() for match in HOST_RE.finditer(sql)]
    sql = HOST_RE.sub("?", sql)
    if len(sql) > 2048:
        raise ValueError("SQL statement exceeds SQLITEX 2048-byte limit")
    if len(inputs) > 8:
        raise ValueError("SQL statement exceeds SQLITEX eight-bind limit")
    if len(outputs) > 16:
        raise ValueError("SELECT exceeds SQLITEX sixteen-column limit")
    return SqlBlock(number, sql, inputs, outputs)


def collect_blocks(lines: list[str]) -> tuple[list[tuple[int, int, str]], list[SqlBlock]]:
    spans: list[tuple[int, int, str]] = []
    blocks: list[SqlBlock] = []
    index = 0
    while index < len(lines):
        if len(lines[index]) > 6 and lines[index][6] in "*/":
            index += 1
            continue
        area = lines[index][6:] if len(lines[index]) > 6 else lines[index]
        start_match = re.match(r"^\s*EXEC\s+SQL\b", area, re.IGNORECASE)
        if not start_match:
            index += 1
            continue
        start = index
        parts = [area[start_match.end():]]
        while "END-EXEC" not in " ".join(parts).upper():
            index += 1
            if index >= len(lines):
                raise ValueError(f"unterminated EXEC SQL at line {start + 1}")
            parts.append(lines[index][6:] if len(lines[index]) > 6 else lines[index])
        joined = " ".join(parts)
        end_at = joined.upper().find("END-EXEC")
        body = normalize_sql(joined[:end_at])
        if body.upper() == "INCLUDE SQLCA":
            spans.append((start, index, "include"))
        else:
            block = parse_statement(len(blocks) + 1, body)
            blocks.append(block)
            spans.append((start, index, str(block.number)))
        index += 1
    return spans, blocks


def fixed(text: str, indent: int = 11) -> str:
    line = " " * indent + text
    if len(line) > 72:
        raise ValueError(f"generated fixed-format COBOL line is too long: {line}")
    return line


def sql_chunks(sql: str, size: int = 44) -> list[str]:
    chunks: list[str] = []
    chunk = ""
    encoded = 0
    for character in sql:
        width = 2 if character == "'" else 1
        if chunk and encoded + width > size:
            chunks.append(chunk)
            chunk = ""
            encoded = 0
        chunk += character
        encoded += width
    chunks.append(chunk)
    return chunks


def include_lines(blocks: list[SqlBlock]) -> list[str]:
    result: list[str] = []
    for copybook in ("SQLITEX.cpy", "SQLISQLC.cpy"):
        result.extend((PROJECT_ROOT / "api" / copybook).read_text(
            encoding="ascii").splitlines())
    for block in blocks:
        result.append(fixed(f"01  {block.constant}.", 7))
        for chunk in sql_chunks(block.sql):
            escaped = chunk.replace("'", "''")
            result.append(fixed(f"05 FILLER PIC X({len(chunk)}) VALUE", 11))
            result.append(fixed(f"'{escaped}'.", 14))
    return result


def generated_call(block: SqlBlock, hosts: dict[str, Host]) -> list[str]:
    for name in block.inputs + block.outputs:
        if name not in hosts:
            raise ValueError(f"host variable {name} has no local PIC declaration")
    result = [
        fixed("MOVE SPACES TO SQLX-REQUEST."),
        fixed("MOVE 0 TO SQLX-BIND-COUNT SQLX-RETURN-CODE."),
        fixed("MOVE 'EXECUTE' TO SQLX-OPERATION."),
        fixed("MOVE 1 TO SQLX-MAX-ROWS."),
        fixed(f"MOVE {block.constant} TO SQLX-SQL."),
        fixed(f"MOVE {len(block.sql)} TO SQLX-SQL-LENGTH."),
        fixed(f"MOVE {len(block.inputs)} TO SQLX-BIND-COUNT."),
    ]
    for position, name in enumerate(block.inputs, 1):
        host = hosts[name]
        result.extend([
            fixed(f"MOVE '{host.kind}' TO SQLX-BIND-TYPE ({position})."),
            fixed(f"MOVE {host.length} TO SQLX-BIND-LENGTH ({position})."),
            fixed(f"MOVE {name} TO SQLX-BIND-VALUE ({position})."),
        ])
    result.extend([
        fixed("CALL 'SQLITEX' USING SQLX-REQUEST."),
        fixed("MOVE SQLX-RETURN-CODE TO SQLCODE."),
        fixed("MOVE SQLX-ROW-COUNT TO SQLROWC."),
        fixed("MOVE SQLX-CHANGES TO SQLCHNG."),
        fixed("MOVE SQLX-MESSAGE TO SQLERRM."),
    ])
    if block.outputs:
        result.extend([
            fixed("IF SQLCODE = 0"),
            fixed("IF SQLROWC = 0", 15),
            fixed("MOVE +100 TO SQLCODE", 19),
            fixed("ELSE", 15),
        ])
        for column, name in enumerate(block.outputs, 1):
            suffix = "." if column == len(block.outputs) else ""
            result.append(fixed(
                f"MOVE SQLX-CELL-VALUE (1, {column}) TO {name}{suffix}", 19
            ))
    return result


def precompile(source: str) -> str:
    lines = source.splitlines()
    hosts = find_hosts(lines)
    spans, blocks = collect_blocks(lines)
    includes = sum(kind == "include" for _, _, kind in spans)
    if blocks and includes != 1:
        raise ValueError("exactly one EXEC SQL INCLUDE SQLCA is required")
    by_start = {start: (end, kind) for start, end, kind in spans}
    numbered = {str(block.number): block for block in blocks}
    output: list[str] = []
    index = 0
    while index < len(lines):
        replacement = by_start.get(index)
        if not replacement:
            output.append(lines[index])
            index += 1
            continue
        end, kind = replacement
        if kind == "include":
            output.extend(include_lines(blocks))
        else:
            output.extend(generated_call(numbered[kind], hosts))
        index = end + 1
    return "\n".join(output) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        generated = precompile(args.input.read_text(encoding="ascii"))
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="ascii")
    print(f"precompiled {args.input} -> {args.output}")


if __name__ == "__main__":
    main()
