# Embedded SQL for KICKS COBOL

KICKS COBOL programs can use a supported subset of familiar embedded SQL.
The host-side precompiler converts each `EXEC SQL ... END-EXEC` block into the
existing typed `SQLITEX` request before the KICKS translator and OS/VS COBOL
compiler run. It embeds the request and SQLCA layouts in its output, so no
COBOL COPY expansion is required. No modification to COBOL or KICKS is needed.

The current subset supports:

- `EXEC SQL INCLUDE SQLCA END-EXEC`
- single-row `SELECT ... INTO :host-variable`
- `INSERT`, `UPDATE`, and `DELETE`
- up to eight input binds and sixteen result columns
- text and integer host variables declared locally with `PIC`
- `SQLCODE`, `SQLROWC`, `SQLCHNG`, and `SQLERRM`

`SQLCODE` is zero on success, `+100` when `SELECT INTO` finds no row, or the
SQLite return code on error. SQL values must use host variables rather than
building quoted strings.

## Example

Declare host variables and SQLCA in `WORKING-STORAGE`:

```cobol
       01  DB-ID       PIC X(5).
       01  DB-NAME     PIC X(20).
           EXEC SQL INCLUDE SQLCA END-EXEC.
```

Use embedded SQL in `PROCEDURE DIVISION`:

```cobol
           EXEC SQL
               SELECT NAME
                 INTO :DB-NAME
                 FROM PEOPLE
                WHERE ID = :DB-ID
           END-EXEC.

           IF SQLCODE = +100
               MOVE 'PERSON NOT FOUND' TO MSGO.

           EXEC SQL
               UPDATE PEOPLE
                  SET NAME = RTRIM(:DB-NAME)
                WHERE ID = :DB-ID
           END-EXEC.
```

The precompiler reads local `PIC` declarations to select bind type and fixed
length. A referenced host variable that has no local declaration is rejected
at build time.

## Build the KICKS example

Generate the fixed-format COBOL source and run the local regression tests:

```sh
make kicks-sql-precompile test-precompiler
```

The editable source is `kicks/sqlite-search/SQLKDETL.cbl`; generated output is
`kicks/sqlite-search/generated/SQLKDETL.cbl`. Upload the generated file, not
the embedded-SQL input:

```sh
zowe zos-files upload file-to-data-set \
  kicks/sqlite-search/generated/SQLKDETL.cbl \
  'IBMUSER.SQLITE.SOURCE(SQLKDETL)' --zosmf-profile hercules
zowe zos-jobs submit local-file kicks/sqlite-search/DBUILD.jcl \
  --zosmf-profile hercules --wait-for-output
```

`EXEC-SQL-TEST.jcl` opens person 1 through the newly generated `SELECT INTO`
path. The ordinary `TEST.jcl` also reaches the detail screen after exercising
the search and orders screens.

`jcl/exec-sql-api-test.jcl` compiles and executes the generated standalone
test. It verifies INSERT, SELECT INTO, UPDATE, DELETE, and `SQLCODE +100`.

## Current boundary

Multi-row queries still use the `SQLITEX` rowset directly. Persistent embedded
SQL cursors are not yet implemented because a KICKS pseudoconversational
terminal round trip must not retain a live SQLite connection. A later cursor
extension should materialize or reissue paged queries rather than keep a
cursor open between transactions.
