# SQLite for MVS 3.8j

Experimental port of SQLite 3.x to MVS 3.8j/Turnkey5 using the MVSLOVERS MBT
toolchain and C370. The intended storage backend is a VSAM RRDS with one
4096-byte SQLite page per relative record number.

**Current SQLite level: 3.53.4.** It is compiled with cc370 and validated on
MVS 3.8j across the SQL regression suite, TSO shell, COBOL APIs, backup and
recovery, concurrent batch writers, and the KICKS people/orders application.

| Role | SQLite version | MVS modules |
|---|---|---|
| Current validated stack | **3.53.4** | `SQLT534`, `SQLI534`, `SQLITEA`, `SQLITEX` |
| Vendored bootstrap baseline | 3.8.11.1 | `SQLTTEST`, `SQLITSO` |

The project is intentionally staged:

1. Compile the upstream SQLite amalgamation with C370.
2. Resolve MVS external-name and assembler-scale constraints. (Prototype done.)
3. Implement `sqlite3_mvs.c` as a SQLite VFS over VSAM RRDS.
4. Add ENQ/DEQ locking, MVS time/randomness, recovery tests, and deployment.

The repository retains SQLite 3.8.11.1 in `vendor/sqlite/` as a small,
reproducible bootstrap baseline. The current 3.53.4 amalgamation is fetched by
the upgrade build and also remains unmodified; platform code and compatibility
shims belong outside upstream `sqlite3.c`.

## Compiler probe

```sh
git submodule update --init --recursive
make sdk
make probe-c
make probe-asm
make probe-link
```

`make probe` runs all three gates for the vendored baseline. It produces a
1.2 MiB object deck and an 857 KiB `SQLTTEST` load module.

## Current status

SQLite **3.53.4** is the current validated project version. The complete stack
passed on Turnkey5/MVS 3.8j, including modern SQL features through
`json_array_insert()`. Build it with:

```sh
tools/probe_sqlite_upgrade.sh 3.53.4 stack
```

The original SQLite 3.8.11.1 bootstrap result is retained for reproducibility.
Its first MVS run returned CC 0000 and printed:

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

The VFS now accepts explicit `DBDD:JRNDD` names for independent RRDS pairs.
This enables SQLite `ATTACH`, online backup/restore through `SQLBAK:SQLBJR`,
and database-specific SYSTEM ENQ resources while preserving the legacy
`SQLDB` to `SQLJRN` mapping.

Rollback journals are stored in a second RRDS allocated as DD `SQLJRN`.
RRN 1 contains VFS metadata and journal byte ranges start at RRN 2. The MVS
test runs with `journal_mode=DELETE` and `synchronous=FULL`; both committed
transactions and an explicit rollback (`rollback_rows=0`) pass across
repeated jobs.

Hot-journal recovery is also exercised on MVS. `jcl/crash.jcl` terminates a
large uncommitted transaction with CC 0012 without closing SQLite. The next
smoke job detects the journal, rolls the database back, reports
`crash_rows=0`, and completes with CC 0000.

An interactive foreground TSO client is included as `SQLI534` for the current
SQLite 3.53.4 stack (`SQL534` command), with legacy `SQLITSO`/`SQLITE` retained
for the vendored baseline. It accepts multi-line SQL, prints query columns and
rows, and supports configurable column/list output, headers, widths, NULL text,
SQL echo, busy timeout, schema/database inspection, and clean PF3 exit. The
`SQL534` CLIST allocates the current-version RRDS pair and invokes `SQLI534`:

```text
SQL534
```

Build the current stack with `tools/probe_sqlite_upgrade.sh 3.53.4 stack`.
Install `clist/SQL534.clist` as `SYS2.CMDPROC(SQL534)`. The legacy baseline is
still built with `make tso` and exposed by `clist/SQLITE.clist`.

`jcl/test-suite.jcl` runs a broader SQL regression suite covering DDL, indexes,
joins, aggregates, NULL handling, rollback, savepoints, constraints, blobs,
views, and foreign-key cascades. It passed on Turnkey5 as JOB01251 with
CC 0000. Busy-timeout retry is verified by `jcl/lock-holder.jcl` plus
`jcl/lock-waiter.jcl`; JOB01254 waited five seconds for the holder, acquired
the lock, and ended CC 0000 while consuming only 0.21 CPU seconds.

Run `jcl/define-rrds.jcl` before the first test and `jcl/smoke.jcl` to execute
`SQLTTEST`. `jcl/print-rrds.jcl` is available for raw record inspection.

See `docs/compiler-probe.md` for the exact configuration and current findings.
See `README-TESTING-MVS.md` for interactive TSO use plus build, deployment,
smoke, persistence, spool, and concurrent-locking test instructions.
See `README-TSO.md` for the complete interactive command reference and TSO
locking workflow.
See `README-TESTDB.md` for the separate 20-person/60-order sample database and
the `TESTDB` TSO command.
See `README-CREATE-DATABASE.md` for creating another empty RRDS database and a
dedicated TSO launcher.
See `README-COBOL-API.md` for the verified OS/VS COBOL `CALL 'SQLITEA'`
interface, copybook, bridge installation, and example job.
See `README-COBOL-STRUCTURED.md` for the typed, parameterized `SQLITEX`
request/row API used by the KICKS people/orders screen.
See `kicks/sqlite-search/README.md` for the BMS/COBOL KICKS example that
searches the sample `people` table by name or city using transaction `SQLS`.
See `docs/upgrade-3.53.4-validation.md` for the complete current-version MVS
validation matrix and job results.

## MBT

MBT is expected as the `mbt/` Git submodule. `mbt.mk` connects the project to
the regular MVSLOVERS build after the compiler probe can produce an object.
The `toolchain/cc370` submodule is used to build the large-input `as370` variant
required by the amalgamation.
