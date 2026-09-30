# SQLite people search for KICKS

This sample is a small KICKS application that calls the SQLite MVS COBOL API.
It searches the `people` table in `IBMUSER.SQLITE.TESTDB` by exact name or
city and displays the result on a BMS screen.

The application follows the same layout as the KICKS `fuel` and `store`
samples: a BMS map, an OS/VS COBOL command-level program, separate PCT/PPT
tables, and a TSO launcher.

## Screen and keys

Start transaction `SQLS`. Enter either a name or a city:

```text
                   *** SQLITE PEOPLE SEARCH FOR KICKS ***

  NAME:  P01               CITY:

 ID | NAME | CITY | AGE
 ----------------------------------------
 1 | P01 | ZAGREB | 21

 ENTER=SEARCH  PF4=CLEAR  PF3=EXIT
```

- `ENTER` searches. If both fields are filled, name takes precedence.
- `PF4` clears both fields and the result area.
- `PF3` leaves the transaction and returns to KICKS.

The search is exact after trimming the ten-character input field. Useful
values from the supplied test database include names `P01` through `P20` and
cities such as `ZAGREB`.

## Files

| File | Purpose |
|---|---|
| `SQLKMAP.bms` | 24x80 BMS map `SQLKSRH` in mapset `SQLKMAP` |
| `SQLKSRCH.cbl` | KICKS COBOL program and `CALL 'SQLITEA'` integration |
| `SETUP.jcl` | Creates the source and application load libraries |
| `MAP.jcl` | Assembles the physical map and creates the COBOL copybook |
| `BUILD.jcl` | Translates, compiles, and links `SQLKSRCH` with the API bridge |
| `PCT.jcl` | Builds PCT suffix `SQ`, including transaction `SQLS` |
| `PPT.jcl` | Builds PPT suffix `SQ`, including program and mapset entries |
| `SQLKICKS.clist` | Allocates SQLite/KICKS files and starts the application region |

## Build and install

First install the SQLite COBOL API as described in
[`README-COBOL-API.md`](../../README-COBOL-API.md), and create the sample
database as described in [`README-TESTDB.md`](../../README-TESTDB.md).

Submit `SETUP.jcl` once. It recreates these dedicated libraries, so do not run
it when they contain changes that have not been saved locally:

```sh
zowe zos-jobs submit local-file kicks/sqlite-search/SETUP.jcl \
  --zosmf-profile hercules --wait-for-output
```

Upload the map and source, then build them in this order:

```sh
zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLKMAP.bms \
  'IBMUSER.SQLITE.SOURCE(SQLKMAP)' --zosmf-profile hercules
zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLKSRCH.cbl \
  'IBMUSER.SQLITE.SOURCE(SQLKSRCH)' --zosmf-profile hercules

zowe zos-jobs submit local-file kicks/sqlite-search/MAP.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/BUILD.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/PCT.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-jobs submit local-file kicks/sqlite-search/PPT.jcl \
  --zosmf-profile hercules --wait-for-output
```

Install the launcher:

```sh
zowe zos-files upload file-to-data-set kicks/sqlite-search/SQLKICKS.clist \
  'SYS2.CMDPROC(SQLKICKS)' --zosmf-profile hercules
```

All four build jobs should end with `CC 0000`. The generated members are in
`IBMUSER.SQLITE.KLOAD`; the SQLite API itself remains in
`IBMUSER.SQLITE.LOAD`.

## Run on TSO

From a clean TSO `READY` prompt:

```text
SQLKICKS
```

After the KICKS startup screen appears, clear it if necessary and enter:

```text
SQLS
```

The launcher binds `SQLDB` to `IBMUSER.SQLITE.TESTDB` and `SQLJRN` to
`IBMUSER.SQLITE.TESTJRN`. To use another database, copy the CLIST and change
both allocations together.

Only one KICKS session using these exact DD-backed datasets should be started
under a TSO user at a time. SQLite's MVS VFS still coordinates database access
with batch and TSO clients through ENQ/DEQ and the configured busy timeout.

## Current scope

This is intentionally a read-only demonstration at the application level.
The underlying API can execute INSERT, UPDATE, and DELETE too, so the next
natural increment is a person detail screen with add/edit/delete operations
and a second screen that lists the selected person's three orders.
