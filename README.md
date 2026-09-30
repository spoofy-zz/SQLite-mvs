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

The first VFS increment maps each 4096-byte SQLite page to the matching VSAM
RRDS relative record number (`page 1 -> RRN 1`). `jcl/define-rrds.jcl` creates
the experimental `IBMUSER.SQLITE.RRDS` cluster. Jobs using the VFS allocate it
as DD `SQLDB`. The MVS smoke test now completes CREATE, INSERT, and SELECT with
CC 0000:

```text
create rc=0
insert rc=0
id=1 value=MVS RRDS
select rc=0
```

The smoke path also enables full auto-vacuum, grows the database with a
12,000-byte value, deletes it, and verifies the reduced page count. The VFS
implements SQLite `xTruncate` by issuing positioned VSAM `ERASE` operations
for RRNs above the new logical end.

SQLite file locking is mapped to MVS SYSTEM-scope ENQ/DEQ resources for
shared readers, the reserved writer, and the pending gate. The RRDS therefore
uses `SHAREOPTIONS(3 3)`, leaving cross-address-space serialization to the
VFS. A two-job test confirmed that `jcl/lock-holder.jcl` can hold
`BEGIN IMMEDIATE` while the class-B `jcl/lock-probe.jcl` receives the expected
`SQLITE_BUSY` instead of writing concurrently.

Rollback journals are stored in a second RRDS allocated as DD `SQLJRN`.
RRN 1 contains VFS metadata and journal byte ranges start at RRN 2. The MVS
test runs with `journal_mode=DELETE` and `synchronous=FULL`; both committed
transactions and an explicit rollback (`rollback_rows=0`) pass across
repeated jobs.

Hot-journal recovery is also exercised on MVS. `jcl/crash.jcl` terminates a
large uncommitted transaction with CC 0012 without closing SQLite. The next
smoke job detects the journal, rolls the database back, reports
`crash_rows=0`, and completes with CC 0000.

An interactive foreground TSO client is now included as load module
`SQLITSO`. It accepts multi-line SQL, prints query columns and rows, and
supports `.help`, `.tables`, `.schema`, `.quit`, and `.exit`. The supplied
`SQLITE` CLIST allocates both RRDS clusters and invokes the client, so an
installed copy starts from a TSO READY prompt with:

```text
SQLITE
```

Build it with `make tso`. The MBT deployment packages both `SQLTTEST` and
`SQLITSO`; install `clist/SQLITE.clist` as `SYS2.CMDPROC(SQLITE)` to expose
the short TSO command.

Run `jcl/define-rrds.jcl` before the first test and `jcl/smoke.jcl` to execute
`SQLTTEST`. `jcl/print-rrds.jcl` is available for raw record inspection.

See `docs/compiler-probe.md` for the exact configuration and current findings.
See `README-TESTING-MVS.md` for interactive TSO use plus build, deployment,
smoke, persistence, spool, and concurrent-locking test instructions.

## MBT

MBT is expected as the `mbt/` Git submodule. `mbt.mk` connects the project to
the regular MVSLOVERS build after the compiler probe can produce an object.
The `toolchain/cc370` submodule is used to build the large-input `as370` variant
required by the amalgamation.
