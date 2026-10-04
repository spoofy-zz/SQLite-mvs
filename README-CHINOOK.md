# Chinook database on MVS

The standard TSO `SQLITE` database (`IBMUSER.SQLITE.D534DB`) can contain the
complete Chinook sample alongside the existing `people` and `orders` tables.
The current imported counts are:

| Table | Rows |
|---|---:|
| artists | 275 |
| albums | 347 |
| tracks | 3503 |
| employees | 8 |
| customers | 59 |
| invoices | 412 |
| invoice_items | 2240 |
| genres | 25 |
| media_types | 5 |
| playlists | 18 |
| playlist_track | 8715 |

## Using it from TSO

At `READY`, start the normal client:

```text
SQLITE
.tables
SELECT count(*) FROM tracks;
SELECT ar.Name, count(*) AS albums
FROM artists ar JOIN albums al ON al.ArtistId=ar.ArtistId
GROUP BY ar.ArtistId,ar.Name ORDER BY albums DESC LIMIT 10;
SELECT c.FirstName,c.LastName,sum(i.Total) AS spent
FROM customers c JOIN invoices i ON i.CustomerId=c.CustomerId
GROUP BY c.CustomerId ORDER BY spent DESC LIMIT 10;
SELECT count(*) FROM people;
SELECT count(*) FROM orders;
```

Chinook identifiers retain their original names. SQLite identifiers are case
insensitive, so `tracks`, `Tracks`, and `TRACKS` address the same table.

## Rebuilding the import

Do not run the import while a TSO shell, KICKS transaction, or batch program
is using `D534DB`. Generate both a readable ASCII script and a fixed-record
EBCDIC upload image:

```sh
python3 tools/build_chinook_import.py \
  /path/to/chinook.db \
  /tmp/chinook-mvs.sql \
  /tmp/chinook-mvs.fb320
```

Create and upload the temporary input dataset:

```sh
zowe zos-files create data-set-sequential IBMUSER.SQLITE.CHINOOK.SQL \
  --record-format FB --record-length 320 --block-size 3200 \
  --size 110TRK --device-type SYSDA --zosmf-profile vhost1
zowe zos-files upload file-to-data-set /tmp/chinook-mvs.fb320 \
  IBMUSER.SQLITE.CHINOOK.SQL --binary --zosmf-profile vhost1
zowe zos-jobs submit local-file jcl/import-chinook.jcl \
  --zosmf-profile vhost1 --wait-for-output
```

`SQLIMPRT` reads `SQLIN`, waits up to 30 seconds for a database lock, and
executes complete statements. The generated script uses one transaction. It
drops only the 11 Chinook tables, imports their data and indexes, and verifies
every Chinook row count plus `people=20` and `orders=60` before commit. Any
SQL or count failure causes a rollback.

MVS uses an EBCDIC code page that cannot represent every Unicode character in
the source database. The generator transliterates names and titles to ASCII
before encoding them as IBM code page 037; relational data and row counts are
unchanged. `IBMUSER.SQLITE.CHINOOK.SQL` is only an import staging dataset and
may be deleted after a successful job.
