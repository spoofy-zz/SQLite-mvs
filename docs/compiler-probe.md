# C370 compiler probe

## Historical vendored baseline

The current validated project version is SQLite **3.53.4**. This document
records the original 3.8.11.1 compiler bring-up, which remains useful as the
small vendored bootstrap probe. See `docs/sqlite-upgrade-ladder.md` for the
current build and version matrix.

- SQLite: 3.8.11.1 (2015-07-29)
- `sqlite3.c` SHA-1: `3be71d99121fe5b17f057011025bcf84e7cc6c84`
- Compiler observed during the first probe: `cc370 V1.0 - Sep 16 2026`
- Language mode: GNU C89, optimization `-O1` (MBT default)
- OS layer: `SQLITE_OS_OTHER`
- Page size target: 4096 bytes

Run `make probe-c` to test the C frontend and `make probe-asm` to test the
generated assembler separately. Complete diagnostics are written below
`build/`.

## First result

The C frontend successfully translated all 161,623 source lines and emitted a
5.5 MiB assembler file (236,426 lines). No unsupported C construct stopped the
translation.

Two toolchain constraints appeared in the unadapted toolchain:

1. External identifiers beginning with `sqlite3_` map to the same short MVS
   external name (`SQLITE3@`). The public API therefore needs an explicit,
   deterministic short-name map or compiler support for unique external names.
2. `as370` reported 154 errors while assembling the single huge translation
   unit. Most are forward references such as `@@F340` whose definitions occur
   much later in the generated file, followed by addressability errors near
   statement 130,968. This looks like a scale/assembler limitation rather than
   a rejected SQLite C construct.

The project now works around both constraints without editing `sqlite3.c`:

- `include/sqlite3_mvs_names.h` maps the public API deterministically to unique
  eight-character external names.
- `tools/shorten_cc370_labels.py` shortens three compiler-private label forms
  once function ordinals reach four digits.
- `patches/cc370-as370-large-input.patch` raises the host assembler's fixed
  statement and relocation tables from 131,072 to 524,288 entries.

With these adaptations, `as370` returns RC 0 and emits a roughly 1.2 MiB object
deck. The adaptations should eventually be proposed upstream; they remain
explicit and reproducible here so development can continue immediately.

## Link and MVS execution

The pinned current `cc370` and `libc370` submodules supply the 64-bit runtime
helpers used by SQLite. `make probe-link` links the core, the initial MVS OS
bootstrap, and `tests/core_link.c` into `SQLTTEST`.

The first deployment installed `SQLTTEST` in `IBMUSER.SQLITE.LOAD`. Turnkey5
job `JOB01174` completed with CC 0000 and emitted `SQLite 3.8.11.1 (3008011)`.
This proves compile, assemble, link, load and execution. It does not yet prove
database I/O; `sqlite3_mvs.c` intentionally registers no VFS at this stage.
