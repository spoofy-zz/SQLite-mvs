#include <stdio.h>
#include <string.h>
#include <time.h>

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

static int verifyZero(void *unused, int columns, char **values, char **names)
{
    printRow(unused, columns, values, names);
    return columns != 1 || values[0] == 0 || strcmp(values[0], "0") != 0;
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

static int runLockTest(sqlite3 *db, int holder)
{
    char *error = 0;
    int rc = sqlite3_exec(db, "PRAGMA journal_mode=OFF; BEGIN IMMEDIATE;",
                          0, 0, &error);
    printf("lock-%s begin rc=%d%s%s\n", holder ? "holder" : "probe", rc,
           error ? " error=" : "", error ? error : "");
    if (error) sqlite3_free(error);
    if (!holder) {
        if (rc == SQLITE_BUSY) {
            printf("lock-probe expected SQLITE_BUSY\n");
            return SQLITE_OK;
        }
        if (rc == SQLITE_OK) sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
        return SQLITE_ERROR;
    }
    if (rc == SQLITE_OK) {
        time_t until = time(0) + 15;
        printf("lock-holder acquired; holding 15 seconds\n");
        while (time(0) < until) { }
        rc = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        printf("lock-holder commit rc=%d\n", rc);
    }
    return rc;
}

static int runCrashTest(sqlite3 *db)
{
    int i;
    int rc;
    rc = runSql(db, "crash-setup",
        "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; "
        "PRAGMA cache_size=2; BEGIN IMMEDIATE; "
        "INSERT INTO smoke(value) VALUES('CRASH PENDING');", 0);
    for (i = 0; rc == SQLITE_OK && i < 40; i++)
        rc = sqlite3_exec(db,
            "INSERT INTO smoke(value) VALUES(zeroblob(3000));",
            0, 0, 0);
    printf("crash-fill rc=%d rows=%d; terminating without COMMIT\n", rc, i);
    fflush(stdout);
    if (rc == SQLITE_OK) exit(12);
    return rc;
}

int main(int argc, char **argv)
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
    if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "HOLD") == 0)
        rc = runLockTest(db, 1);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "PROBE") == 0)
        rc = runLockTest(db, 0);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "CRASH") == 0)
        rc = runCrashTest(db);
    else {
    if (rc == SQLITE_OK) rc = runSql(db, "journal", "PRAGMA journal_mode=DELETE;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "sync", "PRAGMA synchronous=FULL;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "auto-vacuum", "PRAGMA auto_vacuum=FULL;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "create",
        "CREATE TABLE IF NOT EXISTS smoke(id INTEGER PRIMARY KEY,value TEXT);", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "recovery-check",
        "SELECT count(*) AS crash_rows FROM smoke "
        "WHERE value='CRASH PENDING' OR typeof(value)='blob';", verifyZero);
    if (rc == SQLITE_OK) rc = runSql(db, "grow",
        "INSERT INTO smoke(value) VALUES(zeroblob(12000));", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "grown-pages",
        "PRAGMA page_count;", printRow);
    if (rc == SQLITE_OK) rc = runSql(db, "shrink",
        "DELETE FROM smoke WHERE typeof(value)='blob';", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "insert",
        "INSERT INTO smoke(value) VALUES('MVS RRDS');", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "rollback-write",
        "BEGIN; INSERT INTO smoke(value) VALUES('MUST ROLLBACK'); ROLLBACK;", 0);
    if (rc == SQLITE_OK) rc = runSql(db, "rollback-check",
        "SELECT count(*) AS rollback_rows FROM smoke "
        "WHERE value='MUST ROLLBACK';", verifyZero);
    if (rc == SQLITE_OK) rc = runSql(db, "shrunk-pages",
        "PRAGMA page_count;", printRow);
    if (rc == SQLITE_OK) rc = runSql(db, "select",
        "SELECT id,value FROM smoke ORDER BY id DESC LIMIT 1;", printRow);
    }
    if (rc != SQLITE_OK)
        printf("SQLite rc=%d error=%s\n", rc,
               db ? sqlite3_errmsg(db) : "open failed");
    if (db) sqlite3_close(db);
    sqlite3_shutdown();
    return rc == SQLITE_OK ? 0 : 8;
}
