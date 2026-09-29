# SQLite for MVS 3.8j

Experimental port of SQLite 3.x to MVS 3.8j/Turnkey5 using the MVSLOVERS MBT
toolchain and C370. The intended storage backend is a VSAM RRDS with one
4096-byte SQLite page per relative record number.

The project is intentionally staged:

1. Compile the upstream SQLite amalgamation with C370.
2. Resolve MVS external-name and assembler-scale constraints. (Prototype done.)
3. Implement `sqlite3_mvs.c` as a SQLite VFS over VSAM RRDS.
4. Add ENQ/DEQ locking, MVS time/randomness, recovery tests, and deployment.

The pinned baseline is SQLite 3.8.11.1. Its amalgamation is kept unmodified in
`vendor/sqlite/`; platform code and compatibility shims belong outside it.

## Compiler probe

```sh
git submodule update --init --recursive
make sdk
make probe-c
make probe-asm
make probe-link
```

`make probe` runs all three gates. The current baseline produces a 1.2 MiB
object deck and an 857 KiB `SQLTTEST` load module.

## Current status

The full SQLite 3.8.11.1 core compiles, assembles and links with the pinned
cc370/libc370 toolchain. `SQLTTEST` was deployed to `IBMUSER.SQLITE.LOAD` and
executed on Turnkey5/MVS 3.8j; the first run returned CC 0000 and printed:

```text
SQLite 3.8.11.1 (3008011)
```

See `docs/compiler-probe.md` for the exact configuration and current findings.

## MBT

MBT is expected as the `mbt/` Git submodule. `mbt.mk` connects the project to
the regular MVSLOVERS build after the compiler probe can produce an object.
The `toolchain/cc370` submodule is used to build the large-input `as370` variant
required by the amalgamation.
