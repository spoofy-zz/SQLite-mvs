# SQLite TSO command-line program

`SQLITSO` is an interactive SQLite command-line program for a foreground TSO
session on MVS 3.8j. The `SQLITE` CLIST allocates the database and rollback
journal RRDS clusters, calls the load module, and releases both DD names when
the program exits.

## Installation

Build and deploy the load modules from the workstation:

```sh
make tso
PATH=/opt/homebrew/bin:$PWD/build/sdk/bin:/usr/bin:/bin \
  python3 mbt/scripts/mbtdeploy.py \
  --project project.toml --builddir build --ld "$PWD/build/sdk/bin/ld370"
zowe zos-files upload file-to-data-set clist/SQLITE.clist \
  'SYS2.CMDPROC(SQLITE)' --zosmf-profile hercules
```

The default installation uses:

- load module `IBMUSER.SQLITE.LOAD(SQLITSO)`
- database `IBMUSER.SQLITE.RRDS`, allocated as `SQLDB`
- journal `IBMUSER.SQLITE.JOURNAL`, allocated as `SQLJRN`
- command member `SYS2.CMDPROC(SQLITE)`

Start it at a TSO READY prompt:

```text
SQLITE
```

SQL statements may span multiple terminal inputs and execute when a complete
statement ending in `;` has been entered. `.quit`, `.exit`, or PF3 returns to
TSO READY. Clear discards the current incomplete statement.

The separate sample database is opened with `TESTDB` rather than `SQLITE`.
See `README-TESTDB.md` for its schema, seed job, and example joins.

## Shell commands

```text
Command             Description
------------------  --------------------------------
.tables             List tables
.indexes [table]    List indexes, optionally for one table
.schema [table]     Show CREATE statements
.tableinfo table    Show columns, defaults, and primary-key flags
.databases          List attached databases
.foreignkeys [on|off]  Show or change foreign-key enforcement
.stats              Show page and journal statistics
.lastid             Show the last inserted rowid
.headers on|off     Show or hide column headers
.mode column|list|line  Select output format
.separator TEXT     Set the list-mode separator
.width N ...        Set column widths (1-80)
.nullvalue TEXT     Set NULL display text
.echo on|off        Echo SQL before execution
.changes on|off     Show current and total change counts
.timeout MS         Wait for database locks
.show               Show shell settings
.reset              Restore default shell settings
.clear              Clear screen and move cursor home
.version            Show SQLite version
.quit / .exit       Return to TSO READY
```

`column` is the default output mode. Earlier result columns use their
configured widths and the final column is not truncated. `list` prints full
values separated with ` | ` or the value selected by `.separator`. `line`
prints one `column = value` pair per line and is useful for wide rows on a
24x80 terminal.

Example configuration:

```text
.headers on
.mode column
.width 8 24 40
.nullvalue (null)
.changes on
.timeout 15000
.show
```

Useful inspection commands:

```text
.indexes people
.tableinfo people
.foreignkeys
.stats
.mode line
SELECT * FROM people WHERE id=1;
.reset
```

`.foreignkeys` without an argument reports the current SQLite setting.
`.stats` reports the RRDS database page size, allocated page count, freelist
count, journal mode, and synchronous level. `.lastid` reports the connection's
most recent rowid, while `.changes on` adds both current and cumulative change
counts after successful SQL statements.

`.timeout` uses milliseconds. Zero restores immediate `SQLITE_BUSY`. The MVS
VFS sleeps with `STIMER WAIT` between retries, so a waiting TSO session does
not consume CPU in a spin loop.

`.clear` erases the complete 3270 display, returns from full-screen mode, and
places the next `sqlite>` prompt at row 1 with the cursor at the top of the
screen. The physical Clear key performs the same operation and also discards
an incomplete SQL statement.

## Locking and concurrent use

Keep transactions short. To verify TSO-to-batch exclusion, enter this in
`SQLITSO` and leave the transaction open:

```sql
BEGIN IMMEDIATE;
INSERT INTO smoke(value) VALUES('TSO UNCOMMITTED');
```

From the workstation submit `jcl/tso-lock-probe.jcl`. It passes when the batch
probe receives `SQLITE_BUSY`; it must not write concurrently. Back in TSO:

```sql
ROLLBACK;
SELECT count(*) FROM smoke WHERE value='TSO UNCOMMITTED';
```

The count must be zero. For a waiting-client test, set `.timeout 20000`, start
the supplied 15-second batch lock holder, and issue a write from TSO. The write
should complete after the holder commits instead of immediately reporting
`database is locked`.

Only run one command against a given TSO session at a time. Do not delete or
redefine the RRDS clusters while `SQLITSO` is open. If a terminal emulator is
closed without `.quit` or PF3, log off or cancel that TSO address space before
trying to redefine the clusters.

## Direct invocation and troubleshooting

Without the CLIST:

```text
ALLOC FI(SQLDB) DA('IBMUSER.SQLITE.RRDS') SHR
ALLOC FI(SQLJRN) DA('IBMUSER.SQLITE.JOURNAL') SHR
CALL 'IBMUSER.SQLITE.LOAD(SQLITSO)'
FREE FI(SQLDB SQLJRN)
```

`Cannot open SQLite database` usually means a DD allocation failed, the RRDS
has not been defined, or another disconnected TSO address space still owns an
open VSAM control block. Confirm both datasets exist and close the stale TSO
session before retrying.
