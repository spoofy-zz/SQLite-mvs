# SQLite TSO command-line program

The current validated engine is SQLite **3.53.4**, exposed as load module
`SQLI534` and the standard TSO command `SQLITE`. `SQL534` is retained as an
explicit versioned alias. The 3.8.11.1 bootstrap is source-only and is no
longer installed on the host.

`SQLI534` is an interactive SQLite command-line program for a foreground TSO
session on MVS 3.8j. The `SQLITE` CLIST allocates the database and rollback
journal RRDS clusters, calls the load module, and releases both DD names when
the program exits.

## Installation

Build and deploy the load modules from the workstation:

```sh
tools/probe_sqlite_upgrade.sh 3.53.4 stack
PATH=/opt/homebrew/bin:$PWD/build/sdk/bin:/usr/bin:/bin \
  python3 mbt/scripts/mbtdeploy.py \
  --project upgrade/project-3.53.4.toml \
  --builddir build/upgrades/3.53.4/probe \
  --ld "$PWD/build/sdk/bin/ld370"
zowe zos-files upload file-to-data-set clist/SQLITE.clist \
  'SYS2.CMDPROC(SQLITE)' --zosmf-profile hercules
```

The default installation uses:

- load module `IBMUSER.SQLITE.D534.LOAD(SQLI534)`
- database `IBMUSER.SQLITE.D534DB`, allocated as `SQLDB`
- journal `IBMUSER.SQLITE.D534JRN`, allocated as `SQLJRN`
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
.integrity_check    Run a complete database integrity check
.foreign_key_check  Report foreign-key violations
.analyze            Refresh query-planner statistics
.vacuum             Explain the current RRDS/VACUUM limitation
.lastid             Show the last inserted rowid
.headers on|off     Show or hide column headers
.mode column|list|line|csv  Select output format
.separator TEXT     Set the list-mode separator
.width N ...        Set column widths (1-80)
.nullvalue TEXT     Set NULL display text
.echo on|off        Echo SQL before execution
.changes on|off     Show current and total change counts
.timer on|off       Show CPU time used by each SQL statement
.trace on|off       Show statements executed by SQLite
.timeout MS         Wait for database locks
.show               Show shell settings
.reset              Restore default shell settings
.read DDNAME        Execute SQL from an allocated sequential DD
.output DDNAME      Redirect output (`.output terminal` restores TSO)
.once DDNAME        Redirect only the next SQL result
.dump [table]       Export the database or one table as SQL
.backup             Online copy to SQLBAK/SQLBJR
.restore            Replace main from SQLBAK/SQLBJR
.clear              Clear screen and move cursor home
.version            Show active SQLite runtime version and source ID
.quit / .exit       Return to TSO READY
```

For the current `SQLITE` client, `.version` reports the active engine rather
than a documentation constant. Its output begins with:

```text
SQLite runtime
------------------
Version:   3.53.4
Number:    3053004
Source ID: 2026-07-24 ...
Platform:  MVS 3.8j / mvs-rrds
```

`column` is the default output mode. Earlier result columns use their
configured widths and the final column is not truncated. `list` prints full
values separated with ` | ` or the value selected by `.separator`. `line`
prints one `column = value` pair per line and is useful for wide rows on a
24x80 terminal. `csv` applies standard double-quote escaping to commas and
quotes and is intended for redirected output.

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
.integrity_check
.foreign_key_check
.mode line
SELECT * FROM people WHERE id=1;
.reset
```

`.foreignkeys` without an argument reports the current SQLite setting.
`.stats` reports the RRDS database page size, allocated page count, freelist
count, journal mode, and synchronous level. `.lastid` reports the connection's
most recent rowid, while `.changes on` adds both current and cumulative change
counts after successful SQL statements.

## SQL scripts and redirected output

`.read`, `.output`, and `.once` use DD names, not Unix paths. Allocate a
sequential dataset before starting `SQLITE` or `TESTDB`. For example:

```text
ALLOC FI(SQLIN) DA('RVEZ001.SQLITE.SQLIN') SHR
ALLOC FI(SQLOUT) DA('RVEZ001.SQLITE.SQLOUT') OLD
TESTDB
```

Inside the shell:

```text
.read SQLIN
.mode csv
.once SQLOUT
SELECT * FROM people ORDER BY id;
.output SQLOUT
.schema
.dump people
.output terminal
```

Input may contain multi-line SQL, blank lines, `--` comment lines, and shell
dot-commands. Statements still need semicolons. Output DDs are opened for
write, so their previous contents are replaced. `.once` automatically returns
output to the terminal after one SQL execution. Prompts and redirection status
remain visible on the terminal.

`.dump` emits `PRAGMA foreign_keys=OFF`, a transaction, table DDL, typed
`INSERT` statements, and then indexes, triggers, and views. Use it with
`.once` or `.output` for a restorable sequential SQL dataset:

```text
.once SQLOUT
.dump
```

The current shell has a 2048-byte logical output-line limit. A dump row with
a very large TEXT or BLOB value can therefore be truncated; use `.dump` for
ordinary application rows and schema migration, not yet as the physical RRDS
backup mechanism. A page-preserving backup will use the planned second
`SQLBAK`/`SQLBJR` DD pair.

## Administration commands

Run these while no long transaction is open:

```text
.integrity_check
.foreign_key_check
.analyze
.stats
```

An intact database returns one `ok` row from `.integrity_check`.
`.foreign_key_check` returns no rows when there are no violations. `.analyze`
updates SQLite planner statistics through the normal journaled write path.

Full `VACUUM` is deliberately blocked for now. SQLite implements it by opening
a second temporary database, while the current MVS VFS has only one main RRDS
DD pair (`SQLDB`/`SQLJRN`). Aliasing that temporary file to `SQLDB` could
damage the source database. Databases created by this project use
`auto_vacuum=FULL`, so committed deletes already reclaim trailing RRDS pages.
The second-DD mapping now supports backup, restore, and ATTACH. A remaining
VFS change must route SQLite's internally generated VACUUM filename to that
pair before `.vacuum` can safely be enabled.

## RRDS backup, restore, and attached databases

Submit `jcl/define-backup-rrds.jcl` once, install
`clist/SQLITADM.clist` as `SYS2.CMDPROC(SQLITADM)`, and start the administrative
shell with `SQLITADM`. It allocates:

```text
SQLBAK -> IBMUSER.SQLITE.D534BAK
SQLBJR -> IBMUSER.SQLITE.D534BJR
```

Then use:

```text
.backup
.integrity_check
.restore
```

`.backup` and `.restore` use SQLite's online backup API, not a raw VSAM copy.
They are rejected inside an open transaction. The MVS VFS accepts an explicit
database/journal DD pair separated by a colon, which also enables ATTACH:

```sql
ATTACH DATABASE 'AUXDB:AUXJRN' AS aux;
SELECT * FROM aux.some_table;
DETACH DATABASE aux;
```

Both DD names must already be allocated to compatible 4096-byte RRDS
clusters. Each database DD gets independent `DBDD.READ`, `DBDD.WRITE`, and
`DBDD.PENDING` SYSTEM-scope ENQ resources. The legacy filename `SQLDB` still
maps to `SQLDB/SQLJRN`, so existing programs remain compatible.

Backup, restore, and ATTACH were verified by JOB01306: backup integrity
returned `ok`, restore removed a live-only marker, the backup attached as a
second schema, and its schema matched the restored main database.

`.timeout` uses milliseconds. Zero restores immediate `SQLITE_BUSY`. The MVS
VFS sleeps with `STIMER WAIT` between retries, so a waiting TSO session does
not consume CPU in a spin loop.

`.clear` erases the complete 3270 display, returns from full-screen mode, and
places the next `sqlite>` prompt at row 1 with the cursor at the top of the
screen. The physical Clear key performs the same operation and also discards
an incomplete SQL statement.

## Locking and concurrent use

Keep transactions short. To verify TSO-to-batch exclusion, enter this in
`SQLITE` and leave the transaction open:

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
redefine the RRDS clusters while `SQLITE` is open. If a terminal emulator is
closed without `.quit` or PF3, log off or cancel that TSO address space before
trying to redefine the clusters.

## Direct invocation and troubleshooting

Without the CLIST:

```text
ALLOC FI(SQLDB) DA('IBMUSER.SQLITE.D534DB') SHR
ALLOC FI(SQLJRN) DA('IBMUSER.SQLITE.D534JRN') SHR
CALL 'IBMUSER.SQLITE.D534.LOAD(SQLI534)'
FREE FI(SQLDB SQLJRN)
```

`Cannot open SQLite database` usually means a DD allocation failed, the RRDS
has not been defined, or another disconnected TSO address space still owns an
open VSAM control block. Confirm both datasets exist and close the stale TSO
session before retrying.
