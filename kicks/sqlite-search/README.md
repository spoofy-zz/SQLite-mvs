# SQLite people search for KICKS

This sample is a small KICKS application that calls the SQLite MVS COBOL API.
It searches the `people` table in `IBMUSER.SQLITE.TESTDB` by name or city
prefix, displays ten people per page, and displays a person's orders by ID.
It uses typed `SQLITEX` binds and result cells. The detail/CRUD program is now
written with `EXEC SQL` for its single-row SELECT, INSERT, UPDATE, and DELETE
operations; the host precompiler generates the corresponding `SQLITEX` calls.

The application is validated with the current SQLite **3.53.4** stack. Use
`SQLKICKS` to start it.

The application follows the same layout as the KICKS `fuel` and `store`
samples: a BMS map, an OS/VS COBOL command-level program, separate PCT/PPT
tables, and a TSO launcher.

## Screen and keys

Start transaction `SQLS`. Enter either a name or a city:

```text
                   *** SQLITE PEOPLE SEARCH FOR KICKS ***

  NAME:  ANA               CITY:

 ID | NAME | CITY | AGE
 ----------------------------------------
 1 | ANA HORVAT | ZAGREB | 21

 ENTER=SEARCH  PF4=CLEAR  PF3=EXIT
```

- `ENTER` searches people. If both fields are filled, name takes precedence.
- `PF7` displays the previous page and `PF8` the next page.
- `PF5` lists orders for the ID field.
- `PF6` opens the detail/edit screen for the ID field.
- `PF4` clears both fields and the result area.
- `PF3` leaves the transaction and returns to KICKS.

The search matches from the beginning after trimming the ten-character input
field. For example, `ANA` finds `ANA HORVAT`, while `ZAG` finds people in
`ZAGREB`. Blank filters page through all people in ID order.

The screen does not concatenate BMS input into SQL. Name, city, and ID values
are passed through `sqlite3_bind_text`/`sqlite3_bind_int64` by `SQLITEX`.
People and orders paths were verified in KICKS CRLP as JOB01301 (`CC 0000`):
PF5 for person 1 displayed orders 101/BOOK, 102/PEN, and 103/MUG.

## Files

| File | Purpose |
|---|---|
| `SQLKMAP.bms` | 24x80 BMS map `SQLKSRH` in mapset `SQLKMAP` |
| `SQLKSRCH.cbl` | KICKS search program and typed `SQLITEX` integration |
| `SQLDMAP.bms` | Person detail/edit BMS map `SQLDETL` |
| `SQLKDETL.cbl` | Editable person detail/CRUD source with `EXEC SQL` |
| `generated/SQLKDETL.cbl` | Precompiled fixed-format source consumed by MVS |
| `SETUP.jcl` | Creates the source and application load libraries |
| `MAP.jcl` | Assembles the physical map and creates the COBOL copybook |
| `BUILD.jcl` | Translates, compiles, and links `SQLKSRCH` with the API bridge |
| `DMAP.jcl` | Builds the detail/edit BMS map |
| `DBUILD.jcl` | Translates, compiles, and links `SQLKDETL` |
| `PCT.jcl` | Builds PCT suffix `SQ`, including transaction `SQLS` |
| `PPT.jcl` | Builds PPT suffix `SQ`, including program and mapset entries |
| `STARTUP-3.53.4.jcl` | Copies KICKS startup into the SQLite API TASKLIB |
| `TEST.jcl` | Runs a batch CRLP terminal search through KICKS |
| `../../clist/SQLKICKS.clist` | Allocates SQLite/KICKS files and starts the application region |

## Programming SQLite from KICKS COBOL

There are three supported COBOL interfaces. They all open the RRDS database
selected by the `SQLDB` and `SQLJRN` DD names and ultimately use the same
SQLite 3.53.4 engine and MVS VFS.

| Interface | Best use | Parameters and results |
|---|---|---|
| `EXEC SQL` precompiler | Ordinary single-row CRUD in KICKS | COBOL host variables, `SQLCODE`, generated `SQLITEX` calls |
| Direct `SQLITEX` call | Multi-row searches, paging, typed cells | Up to 8 binds, 10 rows, 16 columns per call |
| Direct `SQLITEA` call | Simple utilities and diagnostic output | SQL text in, formatted EBCDIC text out |

New KICKS CRUD code should normally use `EXEC SQL`. Use `SQLITEX` directly
when the program needs a rowset, as `SQLKSRCH` does for ten-row pages. Use
`SQLITEA` when formatted text is sufficient and the program does not need to
address individual typed result cells.

### Embedded `EXEC SQL`

The OS/VS COBOL compiler does not understand embedded SQL itself. Edit the
source containing `EXEC SQL`, then run the host precompiler. It replaces each
SQL block with fixed-format COBOL that populates `SQLITEX`, binds host
variables, calls the bridge, and updates SQLCA fields. Upload only the
generated source to MVS:

```text
SQLKDETL.cbl containing EXEC SQL
        |
        v
tools/sqlite_cobol_precompile.py
        |
        v
generated/SQLKDETL.cbl containing CALL 'SQLITEX'
        |
        v
KICKS translator -> OS/VS COBOL -> link editor
```

Declare every host variable locally in `WORKING-STORAGE` and include SQLCA
once. The precompiler uses each `PIC` declaration to determine the bind type
and fixed length:

```cobol
       01  DB-ID                   PIC X(5) VALUE SPACES.
       01  DB-NAME                 PIC X(20) VALUE SPACES.
       01  DB-CITY                 PIC X(20) VALUE SPACES.
       01  DB-AGE                  PIC X(3) VALUE SPACES.
           EXEC SQL INCLUDE SQLCA END-EXEC.
```

`SELECT INTO` returns one row through host variables:

```cobol
           MOVE WS-CA-ID TO DB-ID.
           EXEC SQL
               SELECT NAME, CITY, AGE
                 INTO :DB-NAME, :DB-CITY, :DB-AGE
                 FROM PEOPLE
                WHERE ID = :DB-ID
           END-EXEC.

           IF SQLCODE = +100
               MOVE 'PERSON NOT FOUND' TO MSGO.
           IF SQLCODE NOT = 0 AND SQLCODE NOT = +100
               MOVE SQLERRM TO INFOO.
```

Input values prefixed with `:` become SQLite bind parameters. They are never
concatenated into SQL text. The output variables after `INTO` receive columns
from left to right. A statement with an undeclared host variable fails during
precompilation.

INSERT, UPDATE, and DELETE use the same host-variable syntax:

```cobol
           EXEC SQL
               INSERT INTO PEOPLE(ID, NAME, CITY, AGE)
               VALUES(:DB-ID, RTRIM(:DB-NAME),
                      RTRIM(:DB-CITY), :DB-AGE)
           END-EXEC.

           EXEC SQL
               UPDATE PEOPLE
                  SET NAME = RTRIM(:DB-NAME),
                      CITY = RTRIM(:DB-CITY),
                      AGE = :DB-AGE
                WHERE ID = :DB-ID
           END-EXEC.

           EXEC SQL
               DELETE FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.
```

`RTRIM` is useful for fixed-width `PIC X` and BMS fields so trailing EBCDIC
spaces are not stored in the database. Validate required BMS input before the
SQL block; the detail program does this in `VALIDATE-FIELDS`.

The generated SQLCA fields are:

| Field | Meaning |
|---|---|
| `SQLCODE` | `0` success, `+100` no row for SELECT INTO, otherwise SQLite error code |
| `SQLROWC` | Number of result rows returned by the call |
| `SQLCHNG` | Rows changed by INSERT, UPDATE, or DELETE |
| `SQLERRM` | SQLite success or error message, 160 bytes |

The current embedded subset is deliberately small: `SELECT INTO`, `INSERT`,
`UPDATE`, and `DELETE`, with at most eight input binds and sixteen selected
columns. It does not yet implement `DECLARE CURSOR`, `OPEN`, `FETCH`, `CLOSE`,
`WHENEVER`, or indicator variables. Each generated call opens and closes the
database, so a cursor or transaction cannot remain active across a KICKS
pseudoconversational terminal return.

### Direct structured `SQLITEX`

Use `SQLITEX` when one call must return several rows. `SQLKSRCH.cbl` uses this
interface for people searches and paging, and `SQLKDETL.cbl` uses it to load a
person's order rows.

Include the request layout and initialize both text and binary fields:

```cobol
           COPY SQLITEX.

           MOVE SPACES TO SQLX-REQUEST.
           MOVE 0 TO SQLX-BIND-COUNT SQLX-RETURN-CODE.
           MOVE 'EXECUTE' TO SQLX-OPERATION.
           MOVE 10 TO SQLX-MAX-ROWS.
```

Prepare SQL with `?` placeholders and bind values by position:

```cobol
           MOVE 'SELECT ID,NAME FROM PEOPLE WHERE CITY=?'
             TO SQLX-SQL.
           MOVE 39 TO SQLX-SQL-LENGTH.
           MOVE 1 TO SQLX-BIND-COUNT.
           MOVE 'T' TO SQLX-BIND-TYPE (1).
           MOVE 6 TO SQLX-BIND-LENGTH (1).
           MOVE 'ZAGREB' TO SQLX-BIND-VALUE (1).
           CALL 'SQLITEX' USING SQLX-REQUEST.
```

Always set `SQLX-SQL-LENGTH` to the actual SQL byte count. Bind types are `T`
text, `I` integer display digits, `F` floating-point display text, `B` bytes,
and `N` NULL. On return, check `SQLX-RETURN-CODE`, then loop from 1 through
`SQLX-ROW-COUNT`; values are in `SQLX-CELL-VALUE (row, column)`, with type and
length in the adjacent cell fields. The current limits are 2048 SQL bytes,
eight binds, ten rows, sixteen columns, and 64 bytes per cell.

Do not initialize the request only with `MOVE SPACES`: OS/VS COBOL binary
`COMP` fields would contain X'4040'. Explicitly move numeric zero into counts
and return-code fields before every call.

### Direct text `SQLITEA`

`SQLITEA` is the simplest interface. It executes a SQL buffer and returns
formatted EBCDIC output with columns separated by ` | `:

```cobol
           COPY SQLITEA.
           MOVE 'SELECT COUNT(*) FROM PEOPLE;' TO SQL-TEXT.
           MOVE 28 TO SQL-LENGTH.
           MOVE 9999 TO OUTPUT-CAPACITY.
           CALL 'SQLITEA' USING SQL-TEXT SQL-LENGTH API-OUTPUT
                OUTPUT-CAPACITY OUTPUT-LENGTH API-RETURN-CODE.
```

Check `API-RETURN-CODE` before displaying or parsing `API-OUTPUT`. This API is
convenient for batch utilities, reports, and diagnostics, but `SQLITEX` or
`EXEC SQL` is preferable for KICKS screens because their parameters and result
columns remain structured.

Every API call currently opens and closes SQLite. When a `SQLITEA` operation
must execute several statements atomically, place the complete transaction in
one input buffer, for example `BEGIN; INSERT ...; COMMIT;`. Separate COBOL
calls are separate database sessions and cannot share an open transaction.

Both direct APIs and generated embedded SQL call the bridge linked from
`IBMUSER.SQLITE.BRG80`. At runtime that bridge loads `SQLITEA` or `SQLITEX`
from `IBMUSER.SQLITE.D534.LOAD`, which is why `STARTUP-3.53.4.jcl` and the
private `KIKSIP1$` TASKLIB setup are required.

For complete interface layouts and standalone tests, also see
[`README-COBOL-EXEC-SQL.md`](../../README-COBOL-EXEC-SQL.md),
[`README-COBOL-STRUCTURED.md`](../../README-COBOL-STRUCTURED.md), and
[`README-COBOL-API.md`](../../README-COBOL-API.md).

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

Precompile the embedded SQL first:

```sh
make kicks-sql-precompile test-precompiler
```

Upload the maps, ordinary search source, generated detail source, and both API
copybooks, then build them in this order:

```sh
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

Install the launcher:

```sh
zowe zos-files upload file-to-data-set clist/SQLKICKS.clist \
  'SYS2.CMDPROC(SQLKICKS)' --zosmf-profile hercules
```

`SQLITEX` must be present for the direct rowset calls in `SQLKSRCH`. The
precompiler embeds both request and SQLCA layouts in generated `SQLKDETL`, so
that member does not rely on COBOL COPY expansion.
All seven build jobs should end with `CC 0000`. The generated members are in
`IBMUSER.SQLITE.KLOAD`; the SQLite API itself remains in
`IBMUSER.SQLITE.D534.LOAD`.

`STARTUP-3.53.4.jcl` is required for the COBOL bridge. The CLIST invokes the private
`IBMUSER.SQLITE.D534.LOAD(KIKSIP1$)` copy, which makes that library the MVS TASKLIB
and lets the bridge's `LOAD EP=SQLITEA` find the API module. Merely placing
`IBMUSER.SQLITE.D534.LOAD` in the KICKS `SKIKLOAD` concatenation is not sufficient
for an MVS LOAD issued by an application program.

For a non-interactive runtime check, submit `TEST.jcl` and inspect its
`CRLPOUT` spool file. It verifies page 1, PF8 page 2, the end-of-results PF8
boundary, PF7 back to page 1, PF4 clear, and PF5 orders for person 1.
`EXEC-SQL-TEST.jcl` directly opens the detail screen and verifies the generated
`SELECT INTO` path. See `README-COBOL-EXEC-SQL.md` for syntax and limits.

The human-name seed and complete KICKS path were verified on MVS 3.8j/Turnkey5
as JOB01365 (`CC 0000`). Page 1 displayed people 1-10, page 2 displayed people
11-20, the last-page boundary and PF7 return worked, and person 1 loaded as
`ANA HORVAT | ZAGREB | 21` with orders 101/BOOK, 102/PEN, and 103/MUG.

The final self-contained embedded-SQL detail source compiled as JOB02650
(`CC 0000`). Its generated `SELECT INTO` path and complete KICKS scenario were
exercised by JOB02651/JOB02649 (`CC 0000`), displaying Ana Horvat, Zagreb,
age 21 and the three expected orders. Standalone generated DML passed as
JOB02647 (`CC 0000`).

## Person detail and CRUD

Enter an ID on `SQLS` and press PF6, or start transaction `SQLD` directly.
On the detail screen:

- Enter loads the ID and its orders.
- PF5 updates NAME, CITY, and AGE for the displayed ID.
- PF6 inserts a new person using all four input fields.
- PF9 arms deletion; PF10 then deletes that person's orders and person row.
- PF3 returns to a new `SQLS` search screen.

Delete deliberately requires two keys. Any action other than PF10 after PF9
cancels the confirmation state. ID, NAME, CITY, and AGE are required for add
and update, and all SQL values use typed `SQLITEX` parameters.

Before binding, the program trims fixed-width BMS name and city inputs and
appends `%`, so trailing BMS padding does not prevent a `LIKE ?` prefix match.
The current offset and active filter are retained in the KICKS COMMAREA across
PF7/PF8 requests.

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

People records support search, detail, insert, update, and confirmed delete.
Orders are displayed and are deleted with their parent person, but do not yet
have their own add/edit screen. Write calls are separately autocommitted by the
current COBOL API, so deleting orders and their person is not yet one atomic
multi-statement transaction.
