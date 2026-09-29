#include <stdio.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

static int printRow(void *unused, int columns, char **values, char **names)
{
    int i;
    (void)unused;
    for (i = 0; i < columns; i++)
        printf("%s=%s%s", names[i], values[i] ? values[i] : "NULL",
               i + 1 == columns ? "\n" : " ");
    return 0;
}

static int runSql(sqlite3 *db, const char *label, const char *sql,
                  sqlite3_callback callback)
{
    char *error = 0;
    int rc = sqlite3_exec(db, sql, callback, 0, &error);
    printf("%s rc=%d%s%s\n", label, rc, error ? " error=" : "",
           error ? error : "");
    if (error) sqlite3_free(error);
    return rc;
}

int main(void)
{
    sqlite3 *db = 0;
    sqlite3_vfs *vfs;
    int rc;
    printf("SQLite %s (%d)\n", sqlite3_libversion(),
           sqlite3_libversion_number());
    rc = sqlite3_initialize();
    vfs = sqlite3_vfs_find("mvs-rrds");
    printf("initialize rc=%d vfs=%p open=%p sz=%d name=%s\n", rc, vfs,
           vfs ? vfs->xOpen : 0, vfs ? vfs->szOsFile : -1,
           vfs ? vfs->zName : "(null)");
    if (rc == SQLITE_OK)
        rc = sqlite3_open_v2("SQLDB", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    printf("open rc=%d db=%p\n", rc, db);
    if (rc == SQLITE_OK) rc = runSql(db, "journal", "PRAGMA journal_mode=OFF;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "sync", "PRAGMA synchronous=OFF;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "create",
        "CREATE TABLE IF NOT EXISTS smoke(id INTEGER PRIMARY KEY,value TEXT);", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "insert",
        "INSERT INTO smoke(value) VALUES('MVS RRDS');", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "select",
        "SELECT id,value FROM smoke ORDER BY id DESC LIMIT 1;", printRow);
    if (rc != SQLITE_OK)
        printf("SQLite rc=%d error=%s\n", rc,
               db ? sqlite3_errmsg(db) : "open failed");
    if (db) sqlite3_close(db);
    sqlite3_shutdown();
    return rc == SQLITE_OK ? 0 : 8;
}
