# SQLite for MVS 3.8j

Experimental port of SQLite 3.x to MVS 3.8j/Turnkey5 using the MVSLOVERS MBT
toolchain and C370. The intended storage backend is a VSAM RRDS with one
4096-byte SQLite page per relative record number.

**Current SQLite level: 3.53.4.** It is compiled with cc370 and validated on
MVS 3.8j across the SQL regression suite, TSO shell, COBOL APIs, backup and
recovery, concurrent batch writers, and the KICKS people/orders application.

| Role | SQLite version | MVS modules |
|---|---|---|
| Current validated stack | **3.53.4** | `SQLT534`, `SQLI534`, `SQLITEA`, `SQLITEX`, `SQLIMPRT` |
| Source-only bootstrap baseline | 3.8.11.1 | `SQLTTEST`, `SQLITSO` |

The project is intentionally staged:

1. Compile the upstream SQLite amalgamation with C370.
2. Resolve MVS external-name and assembler-scale constraints. (Prototype done.)
3. Implement `sqlite3_mvs.c` as a SQLite VFS over VSAM RRDS.
4. Add ENQ/DEQ locking, MVS time/randomness, recovery tests, and deployment.

The repository retains SQLite 3.8.11.1 in `vendor/sqlite/` as a small,
reproducible bootstrap baseline. The current 3.53.4 amalgamation is fetched by
the upgrade build and also remains unmodified; platform code and compatibility
shims belong outside upstream `sqlite3.c`.

## Deploying the complete project to a new MVS

These instructions install SQLite 3.53.4, the seeded VSAM databases, TSO
clients, COBOL bridge, and the KICKS people/orders application on a fresh
Turnkey5/MVS 3.8j system.

The supplied JCL and CLISTs use HLQ `IBMUSER`, volume `TSO003`, command library
`SYS2.CMDPROC`, and KICKS V1R5M0 datasets. Change those names consistently
before starting if the target system differs. The workstation needs Python 3,
Zowe CLI with a `hercules` z/OSMF profile, and Git submodules. The MVS host must
already contain KICKS 1.5 and the OS/VS COBOL toolchain.

### 1. Configure and build

Clone the repository, initialize its submodules, and create `.env` with the
target mvsMF connection. Do not commit this file:

```text
MBT_MVS_HOST=host-name
MBT_MVS_PORT=1080
MBT_MVS_USER=IBMUSER
MBT_MVS_PASS=password
MBT_MVS_HLQ=IBMUSER
```

Build the SDK, SQLite 3.53.4 stack, and COBOL bridge:

```sh
git submodule update --init --recursive
make sdk
tools/probe_sqlite_upgrade.sh 3.53.4 stack
make cobol-bridge
make kicks-sql-precompile test-precompiler
```

The upgrade build produces `SQLT534`, `SQLI534`, `SQLITEA`, `SQLITEX`, and
`SQLIMPRT` in `build/upgrades/3.53.4/probe`. The bridge is
`build/sqliteabr.o`.

### 2. Create VSAM storage and deploy SQLite

The following definition jobs are destructive: rerunning them deletes the
corresponding database or backup pair. On a new system, submit them once:

```sh
zowe zos-jobs submit local-file jcl/define-upgrade-3.53.4.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/define-backup-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/define-vacuum-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
```

Deploy the current modules to `IBMUSER.SQLITE.D534.LOAD`:

```sh
PATH="$PWD/build/sdk/bin:$PATH" \
  python3 mbt/scripts/mbtdeploy.py \
  --project upgrade/project-3.53.4.toml \
  --builddir build/upgrades/3.53.4/probe \
  --ld "$PWD/build/sdk/bin/ld370"
```

Create and upload the FB80 COBOL bridge dataset:

```sh
zowe zos-jobs submit local-file jcl/cobol-api-setup.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-files upload file-to-data-set build/sqliteabr.o \
  IBMUSER.SQLITE.BRG80 --binary --zosmf-profile hercules
```

### 3. Create and seed the databases

Create `TESTDB` with 20 named people and 60 orders. Then install the same
`people`/`orders` sample in the regular database opened by `SQLITE`:

```sh
zowe zos-jobs submit local-file jcl/create-testdb.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/seed-default.jcl \
  --zosmf-profile hercules --wait-for-output
```

`create-testdb.jcl` recreates `IBMUSER.SQLITE.TESTDB` and `TESTJRN`.
`seed-default.jcl` recreates only the two sample tables in `D534DB` and keeps
unrelated tables.

To add the complete Chinook sample to `D534DB` without replacing
`people`/`orders`, follow [README-CHINOOK.md](README-CHINOOK.md). The importer
drops and recreates only the 11 Chinook tables in one transaction.

### 4. Install the TSO commands

```sh
zowe zos-files upload file-to-data-set clist/SQLITE.clist \
  'SYS2.CMDPROC(SQLITE)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set clist/SQL534.clist \
  'SYS2.CMDPROC(SQL534)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set clist/TESTDB.clist \
  'SYS2.CMDPROC(TESTDB)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set clist/SQLITADM.clist \
  'SYS2.CMDPROC(SQLITADM)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set clist/SQLKICKS.clist \
  'SYS2.CMDPROC(SQLKICKS)' --zosmf-profile hercules
```

`SQLITE`, `SQL534`, and `SQLITADM` open `D534DB` through their private
`SQLMAIN`/`SQLMJRN` DD names. `TESTDB` opens the separate KICKS sample through
`SQLDB`/`SQLJRN`. Keeping the DD pairs distinct prevents a KICKS allocation
that is still unwinding from being mistaken for the command-line database.
`SQLITADM` also allocates the backup RRDS pair and uses the distinct
`sqlitadm>` prompt.

Optionally install the supplied Turnkey5/Larry ISPF-like applications panel so
the client is available as option `M.S`:

```sh
zowe zos-files download data-set 'SYS2.ISP.PLIB(TSOAPPLS)' \
  --file TSOAPPLS.backup --zosmf-profile hercules
zowe zos-files upload file-to-data-set isp/TSOAPPLS \
  'SYS2.ISP.PLIB(TSOAPPLS)' --zosmf-profile hercules
```

This panel invokes the installed `SYS2.CMDPROC(SQLITE)` command. Preserve
site-specific changes by merging the SQLite entry when `TSOAPPLS` has already
been customized. See [README-TSO.md](README-TSO.md#larryturnkey5-isp-like-menu)
for the exact panel lines, navigation, and rollback backup.

### 5. Build and install the KICKS application

`SETUP.jcl` recreates `IBMUSER.SQLITE.SOURCE` and `KLOAD`, so do not rerun it
after making unsaved host-side changes:

```sh
zowe zos-jobs submit local-file kicks/sqlite-search/SETUP.jcl \
  --zosmf-profile hercules --wait-for-output

zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLKMAP.bms \
  'IBMUSER.SQLITE.SOURCE(SQLKMAP)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLKSRCH.cbl \
  'IBMUSER.SQLITE.SOURCE(SQLKSRCH)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLDMAP.bms \
  'IBMUSER.SQLITE.SOURCE(SQLDMAP)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set \
  kicks/sqlite-search/generated/SQLKDETL.cbl \
  'IBMUSER.SQLITE.SOURCE(SQLKDETL)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set api/SQLITEX.cpy \
  'KICKS.KICKS.V1R5M0.COBCOPY(SQLITEX)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set api/SQLISQLC.cpy \
  'KICKS.KICKS.V1R5M0.COBCOPY(SQLISQLC)' --zosmf-profile hercules

zowe zos-jobs submit local-file kicks/sqlite-search/MAP.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/BUILD.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/DMAP.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/DBUILD.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/PCT.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/PPT.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/STARTUP-3.53.4.jcl \
  --zosmf-profile hercules --wait-for-output
```

`SQLITEX` is required by the direct rowset source. The embedded-SQL
precompiler places the SQLCA and request layouts directly in generated
`SQLKDETL`; `SQLISQLC` is also installed as the canonical layout for custom
programs. The final startup job copies `KIKSIP1$` into `D534.LOAD`,
allowing the COBOL bridge to load `SQLITEA`/`SQLITEX`. The SQLite deploy
replaces the entire load library, so rerun `STARTUP-3.53.4.jcl` after every
later deploy.

### 6. Verify the installation

Run the non-interactive tests first:

```sh
zowe zos-jobs submit local-file jcl/smoke.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/test-suite.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/vacuum-test.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file jcl/cobol-api-x-test.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/TEST.jcl \
  --zosmf-profile hercules --wait-for-output
```

At TSO `READY`, verify the command-line client:

```text
SQLITE
.version
.tables
SELECT count(*) FROM people;
.quit
```

Start the KICKS application with `SQLKICKS`, then enter transaction `SQLS`.
Search by name or city, use PF6 for detail, and PF3 to leave the transaction.
PF3 returns to KICKS; it does not stop the KICKS region. Enter `K999`, then
press Enter at the `***` pause and wait for `READY` before issuing TSO
commands. The launcher frees the KICKS DD allocations during that return.
See `README-TSO.md` and `kicks/sqlite-search/README.md` for the complete command
and screen references.

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

Full `VACUUM` uses a dedicated `SQLTMP:SQLTJR` RRDS pair instead of aliasing
the source database. The VFS serializes that shared pair with the SYSTEM-scope
`SQLITE/VACUUM` ENQ, clears stale temporary records before every run, and
clears the pair again on close. SQLite's normal rollback transaction protects
the copy back to the main RRDS. `jcl/vacuum-test.jcl` verifies a real compaction,
`integrity_check`, and recovery after an intentionally abandoned temp database;
`vacuum-lock-holder.jcl` plus `vacuum-lock-probe.jcl` verify cross-process
serialization.

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
SQLite 3.53.4 stack. `SQLITE` is the standard command and `SQL534` remains an
explicit versioned alias. It accepts multi-line SQL, prints query columns and
rows, and supports configurable column/list output, headers, widths, NULL text,
SQL echo, busy timeout, schema/database inspection, and clean PF3 exit. Both
CLISTs allocate the current-version RRDS pair and invoke `SQLI534`:

```text
SQLITE
```

Build the current stack with `tools/probe_sqlite_upgrade.sh 3.53.4 stack`.
Install `clist/SQLITE.clist` as `SYS2.CMDPROC(SQLITE)` and optionally install
`clist/SQL534.clist` as `SYS2.CMDPROC(SQL534)`. The 3.8.11.1 baseline remains
in the repository for compiler reproducibility but is no longer deployed.

`jcl/test-suite.jcl` runs a broader SQL regression suite covering DDL, indexes,
joins, aggregates, NULL handling, rollback, savepoints, constraints, blobs,
views, and foreign-key cascades. It passed on Turnkey5 as JOB01251 with
CC 0000. Busy-timeout retry is verified by `jcl/lock-holder.jcl` plus
`jcl/lock-waiter.jcl`; JOB01254 waited five seconds for the holder, acquired
the lock, and ended CC 0000 while consuming only 0.21 CPU seconds.

Run `jcl/define-upgrade-3.53.4.jcl` and `jcl/define-vacuum-rrds.jcl` before the
first test, then use `jcl/smoke.jcl` and `jcl/vacuum-test.jcl` to execute
`SQLT534`. `jcl/print-rrds.jcl` is available for raw record inspection.

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
See `README-COBOL-EXEC-SQL.md` for the KICKS embedded-SQL precompiler,
supported `EXEC SQL` syntax, generated source, and build procedure.
See `kicks/sqlite-search/README.md` for the BMS/COBOL KICKS example that
searches the sample `people` table by name or city using transaction `SQLS`.
See `docs/upgrade-3.53.4-validation.md` for the complete current-version MVS
validation matrix and job results.

## MBT

MBT is expected as the `mbt/` Git submodule. `mbt.mk` connects the project to
the regular MVSLOVERS build after the compiler probe can produce an object.
The `toolchain/cc370` submodule is used to build the large-input `as370` variant
required by the amalgamation.
