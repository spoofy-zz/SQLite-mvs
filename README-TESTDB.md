# SQLite MVS sample database: testdb

`testdb` is a separate sample SQLite database inspired by the `minisql-tso`
PEOPLE/ORDERS demo. On MVS it is stored in two VSAM RRDS clusters:

- `IBMUSER.SQLITE.TESTDB` — SQLite pages
- `IBMUSER.SQLITE.TESTJRN` — rollback journal

The database contains 20 people and exactly three orders for every person,
for a total of 60 order rows.

## Create or reset testdb

The following job deletes any existing testdb clusters, defines clean RRDS
clusters, creates the schema, loads the sample rows, and verifies all counts:

```sh
zowe zos-jobs submit local-file jcl/create-testdb.jcl \
  --zosmf-profile hercules --wait-for-output
```

A successful job ends with CC 0000 and prints:

```text
testdb seed rc=0 people=20 orders=60
suite testdb-people      rc=0 expected=20 seen=1 PASS
suite testdb-orders      rc=0 expected=60 seen=1 PASS
suite testdb-three-each  rc=0 expected=20 seen=1 PASS
suite testdb-first-name  rc=0 expected=ANA HORVAT seen=1 PASS
suite testdb-last-name   rc=0 expected=MARIO KOS seen=1 PASS
SQLITE TESTDB CREATED
```

This operation destroys and recreates testdb. It does not modify the regular
`IBMUSER.SQLITE.D534DB` database used by the `SQLITE` command.

To install the same PEOPLE/ORDERS sample in the regular database opened by
`SQLITE`, submit `jcl/seed-default.jcl`. That job recreates only `people` and
`orders`; unrelated tables in `D534DB` are preserved.

## Open from TSO

Install the command CLIST once:

```sh
zowe zos-files upload file-to-data-set clist/TESTDB.clist \
  'SYS2.CMDPROC(TESTDB)' --zosmf-profile hercules
```

At TSO READY:

```text
TESTDB
```

The second table is named `orders` so it can be used without quoting an SQL
keyword.

Useful queries:

```sql
.tables
SELECT count(*) AS people_count FROM people;
SELECT count(*) AS order_count FROM orders;
SELECT p.id,p.name,p.city,o.id,o.item,o.quantity,o.amount
FROM people p JOIN orders o ON o.people_id=p.id
ORDER BY p.id,o.id;
SELECT p.name,count(*) AS orders,sum(o.amount) AS total
FROM people p JOIN orders o ON o.people_id=p.id
GROUP BY p.id,p.name ORDER BY p.id;
.quit
```

## Schema

```sql
CREATE TABLE people(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  city TEXT,
  age INTEGER,
  email TEXT UNIQUE
);

CREATE TABLE orders(
  id INTEGER PRIMARY KEY,
  people_id INTEGER NOT NULL,
  item TEXT NOT NULL,
  quantity INTEGER NOT NULL,
  amount INTEGER NOT NULL,
  FOREIGN KEY(people_id) REFERENCES people(id)
);

CREATE INDEX orders_people ON orders(people_id);
```

The 20 rows use human names from `ANA HORVAT` through `MARIO KOS`. Cities
rotate through Zagreb, Split, Rijeka, Osijek, and Pula. Each person has BOOK,
PEN, and MUG order rows.
