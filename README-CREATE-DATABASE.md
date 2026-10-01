# Creating another SQLite database on MVS

An SQLite MVS database consists of two VSAM RRDS clusters and two DD names:

- `SQLDB` selects the RRDS containing 4096-byte SQLite pages.
- `SQLJRN` selects the RRDS used for the rollback journal.

The SQLite filename passed by `SQLI534` is a logical name. The actual database
is selected by the datasets allocated to these DD names. Therefore no VFS or
load-module rebuild is needed for each new database.

## 1. Choose names

This example creates a database named `MYDB`:

```text
Database cluster: YOURID.SQLITE.MYDB
Journal cluster:  YOURID.SQLITE.MYJRN
TSO command:      MYDB
```

Replace `YOURID` with the target high-level qualifier. Dataset names must obey
normal MVS naming rules and remain within 44 characters.

## 2. Define the RRDS clusters

Copy `jcl/create-database.template.jcl`, replace `YOURID`, `MYDB`, and
`MYJRN`, then submit it:

```sh
zowe zos-jobs submit local-file jcl/create-mydb.jcl \
  --zosmf-profile hercules --wait-for-output
```

Both clusters must use fixed 4096-byte records, `NUMBERED`, and
`SHAREOPTIONS(3 3)`. The template allocates 20 primary tracks for the database
and 10 for the journal; adjust `TRACKS(primary secondary)` for the expected
size. Submitting the template again deletes all existing data.

## 3. Create a TSO launcher

Copy `clist/DATABASE.template.clist`, replace both dataset names, and upload it
under the desired command member. For command `MYDB`:

```sh
zowe zos-files upload file-to-data-set clist/MYDB.clist \
  'SYS2.CMDPROC(MYDB)' --zosmf-profile hercules
```

The CLIST allocates the selected clusters as `SQLDB` and `SQLJRN`, plus the
installation-wide `IBMUSER.SQLITE.D534TMP`/`D534TJR` pair as `SQLTMP` and
`SQLTJR`. It calls the shared `IBMUSER.SQLITE.D534.LOAD(SQLI534)` module, then
frees all four DD names. Define the shared pair once with
`jcl/define-vacuum-rrds.jcl`.

## 4. Initialize the schema

At TSO READY:

```text
MYDB
```

The first open initializes an empty SQLite database. Create tables normally:

```sql
PRAGMA foreign_keys=ON;
CREATE TABLE customer(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  city TEXT
);
CREATE INDEX customer_city ON customer(city);
INSERT INTO customer(name,city) VALUES('ANA','ZAGREB');
.tables
.schema customer
.quit
```

Open the same command later to reuse the persisted database.

## Batch access

Any batch program using this VFS selects the same database with DD statements:

```jcl
//STEPLIB DD DISP=SHR,DSN=IBMUSER.SQLITE.D534.LOAD
//SQLDB   DD DISP=SHR,DSN=YOURID.SQLITE.MYDB
//SQLJRN  DD DISP=SHR,DSN=YOURID.SQLITE.MYJRN
//SQLTMP  DD DISP=SHR,DSN=IBMUSER.SQLITE.D534TMP
//SQLTJR  DD DISP=SHR,DSN=IBMUSER.SQLITE.D534TJR
```

Do not redefine or delete either cluster while TSO or batch has it open.
SQLite locking is implemented with SYSTEM-scope ENQ/DEQ. In the current VFS,
all databases share the same lock resource names, so independent databases are
safe but conservatively serialize writers with one another.
