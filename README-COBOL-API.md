# SQLite MVS COBOL API

`SQLITEA` lets an OS/VS COBOL program execute SQL against the SQLite RRDS
database selected by DD names `SQLDB` and `SQLJRN`. The implementation uses
the real SQLite 3.8.11.1 engine and the same MVS VFS as `SQLITSO`.

This is the first API version: one COBOL call executes one SQL buffer and
returns formatted EBCDIC text. It deliberately establishes and tears down a C
runtime for every call, following the proven `minisql-api` bridge design.

## Calling contract

```cobol
CALL 'SQLITEA' USING SQL-TEXT SQL-LENGTH API-OUTPUT
     OUTPUT-CAPACITY OUTPUT-LENGTH API-RETURN-CODE.
```

| Parameter | COBOL type | Direction | Meaning |
|---|---|---|---|
| `SQL-TEXT` | `PIC X(4096)` | input | SQL text, not NUL terminated |
| `SQL-LENGTH` | `PIC S9(4) COMP` | input | Number of SQL bytes |
| `API-OUTPUT` | `PIC X(9999)` | output | EBCDIC result or error text |
| `OUTPUT-CAPACITY` | `PIC S9(4) COMP` | input | Available output bytes |
| `OUTPUT-LENGTH` | `PIC S9(4) COMP` | output | Bytes actually returned |
| `API-RETURN-CODE` | `PIC S9(4) COMP` | output | SQLite result code |

All numeric parameters are signed two-byte binary fields. Do not substitute
`PIC S9(5) COMP`, which occupies four bytes with OS/VS COBOL. The reusable
definitions are in `api/SQLITEA.cpy`.

Successful queries return a header followed by rows. Columns are separated by
` | ` and records by EBCDIC newline X'15'. Successful statements without rows
return `OK changes=N`. Errors return SQLite's numeric code and an `ERROR:`
message. Bridge load failure returns 16.

## Runtime architecture

The COBOL application is linked with `build/sqliteabr.o`. Its small `SQLITEA`
CSECT performs MVS `LOAD EP=SQLITEA`, calls the separately deployed API load
module, and balances it with `DELETE EP=SQLITEA`.

The API module uses `crt1` plus a custom `@@START` adapter. It obtains the
original COBOL parameter list, removes MVS's final-address flag, initializes
SQLite, opens the DD-backed database, executes SQL, closes SQLite, and returns
through the bridge.

Because each call opens and closes the database, a transaction that must span
multiple statements must place those statements in one SQL buffer:

```cobol
MOVE 'BEGIN; INSERT INTO T VALUES(1); COMMIT;' TO SQL-TEXT.
```

A future resident API will add prepared statement handles and row-by-row
`PREPARE`/`FETCH` operations. Those are not part of this v1 contract.

## Build and installation

Build the API load module and bridge:

```sh
make cobol-api cobol-bridge
```

Normal MBT deployment now installs four members in
`IBMUSER.SQLITE.LOAD`: `SQLTTEST`, `SQLITSO`, `SQLITEA`, and `SQLITEX`.

The bridge must be a sequential FB80 object dataset. The combined bridge
exports both `SQLITEA` and `SQLITEX`. Create and upload it:

```sh
zowe zos-jobs submit local-file jcl/cobol-api-setup.jcl \
  --zosmf-profile hercules --wait-for-output
zowe zos-files upload file-to-data-set build/sqliteabr.o \
  IBMUSER.SQLITE.BRG80 --binary --zosmf-profile hercules
```

## Compile and run the example

`cobol/SQLATST.cbl` contains the standalone example. For convenient testing,
the same source is embedded in `jcl/cobol-api-test.jcl`. Submit it with:

```sh
zowe zos-jobs submit local-file jcl/cobol-api-test.jcl \
  --zosmf-profile hercules --wait-for-output
```

The job compiles with `IKFCBL00`, combines the COBOL object with
`IBMUSER.SQLITE.BRG80` using LOADER, and runs against the sample `testdb`.
The execution step needs `REGION=4096K` because the SQLite amalgamation load
module is substantially larger than the MiniSQL API module.

Expected output:

```text
SQLITE COBOL API TEST START
API RC: 0000
PEOPLE
20
API RC: 0000
ORDERS
60
SQLITE COBOL API TEST END
```

Verified on MVS 3.8j/Turnkey5 as JOB01264: COBOL compile CC 0000 and execution
CC 0000.

For a command-level KICKS integration using this same calling contract, see
[`kicks/sqlite-search/README.md`](kicks/sqlite-search/README.md). It includes a
BMS search screen, transaction definitions, build JCL, and a TSO launcher.

## Required execution DD statements

```jcl
//STEPLIB DD DISP=SHR,DSN=IBMUSER.SQLITE.LOAD
//SQLDB   DD DISP=SHR,DSN=IBMUSER.SQLITE.TESTDB
//SQLJRN  DD DISP=SHR,DSN=IBMUSER.SQLITE.TESTJRN
```

Change `SQLDB` and `SQLJRN` together to select another database. The load
module itself is shared by every database.
