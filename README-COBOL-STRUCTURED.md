# Structured COBOL API (`SQLITEX`)

`SQLITEX` is the typed companion to the original text-oriented `SQLITEA`
entry point. A COBOL program supplies one request block containing SQL and up
to eight bind values. The response contains column metadata and up to ten rows
of typed cells, so KICKS programs no longer parse `value | value` text or
insert terminal input into SQL literals.

```cobol
COPY SQLITEX.

MOVE SPACES TO SQLX-REQUEST.
MOVE 0 TO SQLX-BIND-COUNT SQLX-RETURN-CODE.
MOVE 'SELECT ID,NAME,CITY FROM PEOPLE WHERE CITY=?' TO SQLX-SQL.
MOVE 44 TO SQLX-SQL-LENGTH.
MOVE 1 TO SQLX-BIND-COUNT.
MOVE 'T' TO SQLX-BIND-TYPE (1).
MOVE 6 TO SQLX-BIND-LENGTH (1).
MOVE 'ZAGREB' TO SQLX-BIND-VALUE (1).
MOVE 10 TO SQLX-MAX-ROWS.
CALL 'SQLITEX' USING SQLX-REQUEST.
```

Bind and cell types are `T` (text), `I` (integer represented as display
digits), `F` (floating-point display text), `B` (raw bytes), and `N` (NULL).
Each cell has a type, a two-byte length, and 64 bytes of value storage. The
fixed limits are 2048 SQL bytes, eight bind parameters, sixteen columns, ten
rows, and 64 bytes per cell.

`api/SQLITEX.cpy` is the canonical OS/VS COBOL layout. Initialize COMP fields
with numeric zero; `MOVE SPACES TO SQLX-REQUEST` alone puts X'4040' into those
fields and is not a numeric initialization.

The implementation prepares one statement, applies `sqlite3_bind_*`, steps
rows, copies SQLite types, finalizes, and returns. It is structured and safe
for KICKS input, but this first v2 increment still opens and closes the C
runtime/database within each call. True resident handles spanning COBOL calls
need a long-lived service task or mailbox because libc370's `crt1` teardown
releases the runtime heap when the loaded module returns. That lifecycle is
not falsely exposed as a persistent pointer in this ABI.

Build with `make cobol-api-x cobol-bridge`. Normal MBT deployment installs
`SQLITEX`; the combined `sqliteabr.o` bridge now exports both `SQLITEA` and
`SQLITEX`. `jcl/cobol-api-x-test.jcl` is the executable example.

Verified on MVS 3.8j as JOB01294: four `ZAGREB` rows, three columns, typed
first row `1 / P01 / ZAGREB`, and execution RC 0000.
