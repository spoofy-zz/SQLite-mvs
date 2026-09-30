# Testing SQLite on MVS 3.8j

These commands are run from the project directory on the workstation. They
submit the supplied JCL through the Zowe profile named `hercules`.

## Build and deploy

Build the host-side cc370/libc370 SDK once, then compile, link, and deploy the
`SQLTTEST` batch test and `SQLITSO` interactive load modules:

```sh
git submodule update --init --recursive
make sdk
make probe
make tso
PATH=/opt/homebrew/bin:$PWD/build/sdk/bin:/usr/bin:/bin \
  python3 mbt/scripts/mbtdeploy.py \
  --project project.toml --builddir build --ld "$PWD/build/sdk/bin/ld370"
```

The deploy target is `IBMUSER.SQLITE.LOAD`.

## Interactive TSO client

Upload the supplied CLIST once (use a different command library if
`SYS2.CMDPROC` is not in your site's TSO command search path):

```sh
zowe zos-files upload file-to-data-set clist/SQLITE.clist \
  'SYS2.CMDPROC(SQLITE)' --zosmf-profile hercules
```

At a TSO READY prompt, enter:

```text
SQLITE
```

The expected banner is:

```text
SQLite 3.8.11.1 for MVS TSO
Use .help for commands
sqlite>
```

Example session:

```text
sqlite> .tables
name
------------------
smoke
1 row
sqlite> SELECT id,value FROM smoke ORDER BY id DESC LIMIT 2;
id                  value
------------------  ------------------
2                   MVS RRDS
1                   MVS RRDS
2 rows
sqlite> .schema smoke
sql
------------------
CREATE TABLE smoke(id INTEGER PRIMARY KEY, value TEXT)
1 row
sqlite> .quit
SQLite TSO session ended
```

SQL can span multiple input lines; execution starts when
`sqlite3_complete()` sees a terminating semicolon. Available shell commands
include `.help`, `.tables`, `.schema [table]`, `.databases`, `.version`,
`.headers on|off`, `.mode column|list`, `.quit`, and `.exit`. Column mode is
the default and underlines its headers; list mode preserves full values and
separates fields with ` | `. PF3 is an immediate exit key; Clear discards a
partially entered SQL statement and clears the display. `.clear` performs the
same screen reset from the command line.

To invoke the load module without installing the CLIST, use these commands
from TSO READY:

```text
ALLOC FI(SQLDB) DA('IBMUSER.SQLITE.RRDS') SHR
ALLOC FI(SQLJRN) DA('IBMUSER.SQLITE.JOURNAL') SHR
CALL 'IBMUSER.SQLITE.LOAD(SQLITSO)'
FREE FI(SQLDB SQLJRN)
```

Do not run `jcl/define-rrds.jcl` while the interactive client is open: that
job deletes and recreates both VSAM clusters. The client is a foreground TSO
program because its input/output wrappers use the TGET and TPUT services; it
is not intended to run under batch JCL.

## Create a clean database

This deletes and recreates the test VSAM clusters, so do not run it when their
contents must be preserved:

```sh
zowe zos-jobs submit local-file jcl/define-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
```

Expected result: `SQLTDEF`, `CC 0000`.

## SQL smoke test

```sh
zowe zos-jobs submit local-file jcl/smoke.jcl \
  --zosmf-profile hercules --wait-for-output
```

The job `SQLTVSAM` must end with `CC 0000`. Its `SYSPRINT` includes the SQLite
version, return code for every SQL phase, page counts before and after
auto-vacuum, and the selected row. A clean first run currently ends with:

```text
grown-pages rc=0
shrink rc=0
insert rc=0
rollback-write rc=0
rollback_rows=0
rollback-check rc=0
page_count=3
shrunk-pages rc=0
id=1 value=MVS RRDS
select rc=0
```

Run the same smoke JCL again without running `define-rrds.jcl`. The selected
row should then have `id=2`, proving persistence across MVS jobs.

## Concurrent-writer locking test

Start the class-A holder without waiting for it to finish:

```sh
zowe zos-jobs submit local-file jcl/lock-holder.jcl \
  --zosmf-profile hercules
```

While it is holding `BEGIN IMMEDIATE`, submit the class-B probe:

```sh
zowe zos-jobs submit local-file jcl/lock-probe.jcl \
  --zosmf-profile hercules --wait-for-output
```

The probe passes when it ends with `CC 0000` and reports:

```text
lock-probe begin rc=5 error=database is locked
lock-probe expected SQLITE_BUSY
```

The holder should also end with `CC 0000` after reporting that it acquired the
lock, held it for 15 seconds, and committed. Classes A and B are intentional:
they allow both jobs to execute in different initiators at the same time.

## SQL regression suite

Run the independent DDL/DML regression suite after the ordinary smoke test:

```sh
zowe zos-jobs submit local-file jcl/test-suite.jcl \
  --zosmf-profile hercules --wait-for-output
```

`SQLTSUIT` tests tables, indexes, joins, aggregates, NULL values, updates,
transaction rollback, savepoints, UNIQUE constraints, blobs, views, foreign
key cascades, and cleanup. It passes with `CC 0000` and the final line:

```text
SQLITE MVS TEST SUITE PASSED
```

## TSO-to-batch locking test

Start `SQLITE` at TSO READY, then hold an uncommitted writer transaction:

```sql
BEGIN IMMEDIATE;
INSERT INTO smoke(value) VALUES('TSO UNCOMMITTED');
```

While TSO is waiting at its next prompt, submit:

```sh
zowe zos-jobs submit local-file jcl/tso-lock-probe.jcl \
  --zosmf-profile hercules --wait-for-output
```

The batch probe must finish with `CC 0000` and report the expected
`SQLITE_BUSY`. Return to TSO and enter `ROLLBACK;`; the marker row must not be
visible. This verifies that the SYSTEM-scope ENQ resources cover foreground
TSO and batch address spaces, not only two batch initiators.

To verify waiting rather than immediate failure, enter `.timeout 20000` in
TSO, submit `jcl/lock-holder.jcl`, wait for its `lock-holder acquired` message,
and issue an INSERT from TSO. The operation should finish after the holder's
15-second commit. The VFS uses a timed MVS wait between retries.

The same timeout path has an automated two-initiator regression. Submit
`jcl/lock-holder.jcl`, then submit `jcl/lock-waiter.jcl` while the holder is
active. `SQLTWAIT` uses a 30-second busy timeout and passes when it reports
`lock-wait acquired after holder release` with CC 0000.

## Hot-journal recovery test

Start from a successful smoke test so the `smoke` table exists. Then submit
the deliberate crash job:

```sh
zowe zos-jobs submit local-file jcl/crash.jcl \
  --zosmf-profile hercules --wait-for-output
```

`SQLTCRSH` intentionally terminates with `CC 0012` after filling a transaction
without committing or closing SQLite. Run `jcl/smoke.jcl` immediately
afterward. Recovery passes when that job ends with `CC 0000` and prints
`crash_rows=0`, proving that the hot journal removed the uncommitted rows.

## Read job output

Replace `JOBnnnnn` with the returned job ID:

```sh
zowe zos-jobs view all-spool-content JOBnnnnn \
  --zosmf-profile hercules
```

Check `JESMSGLG`/`JESYSMSG` for the condition code and `SYSPRINT` for SQLite
output. To inspect the first database RRDS record in hexadecimal, submit:

```sh
zowe zos-jobs submit local-file jcl/print-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
```

## Required MVS allocations

The test program is `SQLTTEST`. At minimum its JCL needs:

```jcl
//STEPLIB DD DISP=SHR,DSN=IBMUSER.SQLITE.LOAD
//SQLDB   DD DISP=SHR,DSN=IBMUSER.SQLITE.RRDS
//SQLJRN  DD DISP=SHR,DSN=IBMUSER.SQLITE.JOURNAL
```
