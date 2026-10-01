#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

static int expectValue(sqlite3 *db, const char *label, const char *sql,
                       const char *value);

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

static int runWaitTest(sqlite3 *db)
{
    char *error = 0;
    time_t started = time(0);
    int rc;
    sqlite3_busy_timeout(db, 30000);
    rc = sqlite3_exec(db,
        "PRAGMA journal_mode=DELETE; BEGIN IMMEDIATE; COMMIT;",
        0, 0, &error);
    printf("lock-wait rc=%d elapsed=%ld%s%s\n", rc,
           (long)(time(0) - started), error ? " error=" : "",
           error ? error : "");
    if (error) sqlite3_free(error);
    if (rc == SQLITE_OK) printf("lock-wait acquired after holder release\n");
    return rc;
}

static int copyDatabase(sqlite3 *destination, sqlite3 *source)
{
    sqlite3_backup *copy = sqlite3_backup_init(destination, "main",
                                                source, "main");
    int rc;
    if (!copy) return sqlite3_errcode(destination);
    rc = sqlite3_backup_step(copy, -1);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    if (sqlite3_backup_finish(copy) != SQLITE_OK && rc == SQLITE_OK)
        rc = sqlite3_errcode(destination);
    return rc;
}

static int runBackupTest(sqlite3 *db)
{
    sqlite3 *backup = 0;
    int rc = sqlite3_open_v2("SQLBAK:SQLBJR", &backup,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    printf("backup open rc=%d\n", rc);
    if (rc == SQLITE_OK) rc = copyDatabase(backup, db);
    printf("backup copy rc=%d\n", rc);
    if (rc == SQLITE_OK) rc = expectValue(backup, "backup-integrity",
        "PRAGMA integrity_check;", "ok");
    if (rc == SQLITE_OK) rc = runSql(db, "backup-marker",
        "INSERT INTO smoke(value) VALUES('BACKUP RESTORE MARKER');", 0);
    if (rc == SQLITE_OK) rc = copyDatabase(db, backup);
    printf("restore copy rc=%d\n", rc);
    if (rc == SQLITE_OK) rc = expectValue(db, "restore-marker",
        "SELECT count(*) FROM smoke WHERE value='BACKUP RESTORE MARKER';",
        "0");
    if (backup) {
        sqlite3_close(backup);
        backup = 0;
    }
    if (rc == SQLITE_OK) rc = runSql(db, "attach-backup",
        "ATTACH DATABASE 'SQLBAK:SQLBJR' AS backup;", 0);
    if (rc == SQLITE_OK) rc = expectValue(db, "attach-schema",
        "SELECT (SELECT count(*) FROM backup.sqlite_master)="
        "(SELECT count(*) FROM main.sqlite_master);", "1");
    if (rc == SQLITE_OK) rc = runSql(db, "detach-backup",
        "DETACH DATABASE backup;", 0);
    if (backup) sqlite3_close(backup);
    printf("SQLITE BACKUP TEST %s\n", rc == SQLITE_OK ? "PASSED" : "FAILED");
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

typedef struct ExpectedValue ExpectedValue;
struct ExpectedValue {
    const char *value;
    int seen;
    int matched;
};

static int checkValue(void *context, int columns, char **values, char **names)
{
    ExpectedValue *expected = (ExpectedValue *)context;
    (void)names;
    expected->seen++;
    if (columns == 1 && values[0] && strcmp(values[0], expected->value) == 0)
        expected->matched = 1;
    return 0;
}

static int expectValue(sqlite3 *db, const char *label, const char *sql,
                       const char *value)
{
    ExpectedValue expected;
    char *error = 0;
    int rc;
    expected.value = value;
    expected.seen = 0;
    expected.matched = 0;
    rc = sqlite3_exec(db, sql, checkValue, &expected, &error);
    printf("suite %-18s rc=%d expected=%s seen=%d %s%s%s\n", label, rc,
           value, expected.seen, expected.matched ? "PASS" : "FAIL",
           error ? " error=" : "", error ? error : "");
    if (error) sqlite3_free(error);
    return rc == SQLITE_OK && expected.seen == 1 && expected.matched
           ? SQLITE_OK : SQLITE_ERROR;
}

static int suiteSql(sqlite3 *db, const char *label, const char *sql)
{
    int rc = runSql(db, label, sql, 0);
    printf("suite %-18s %s\n", label, rc == SQLITE_OK ? "PASS" : "FAIL");
    return rc;
}

static int runModernSuite(sqlite3 *db)
{
    char *error = 0;
    int version = sqlite3_libversion_number();
    int rc = SQLITE_OK;
    printf("suite modern version=%d\n", version);
    if (version >= 3024000) {
        rc = suiteSql(db, "upsert-setup",
            "DROP TABLE IF EXISTS ts_modern; CREATE TABLE ts_modern("
            "id INTEGER PRIMARY KEY,value INTEGER);"
            "INSERT INTO ts_modern VALUES(1,10);"
            "INSERT INTO ts_modern VALUES(1,20) ON CONFLICT(id) "
            "DO UPDATE SET value=excluded.value;");
        if (rc == SQLITE_OK) rc = expectValue(db, "upsert",
            "SELECT value FROM ts_modern WHERE id=1;", "20");
    }
    if (rc == SQLITE_OK && version >= 3025000)
        rc = expectValue(db, "window",
            "SELECT row_number() OVER (ORDER BY id) FROM ts_modern "
            "WHERE id=1;", "1");
    if (rc == SQLITE_OK && version >= 3031000) {
        rc = suiteSql(db, "generated-setup",
            "DROP TABLE ts_modern; CREATE TABLE ts_modern("
            "value INTEGER,doubled INTEGER GENERATED ALWAYS AS "
            "(value*2) STORED); INSERT INTO ts_modern(value) VALUES(7);");
        if (rc == SQLITE_OK) rc = expectValue(db, "generated",
            "SELECT doubled FROM ts_modern;", "14");
    }
    if (rc == SQLITE_OK && version >= 3035000)
        rc = expectValue(db, "returning",
            "INSERT INTO ts_modern(value) VALUES(9) RETURNING value;", "9");
    if (rc == SQLITE_OK && version >= 3037000) {
        rc = suiteSql(db, "strict-setup",
            "DROP TABLE ts_modern; CREATE TABLE ts_modern(value INTEGER) "
            "STRICT;");
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(db,
                "INSERT INTO ts_modern VALUES('not-an-integer');",
                0, 0, &error);
            printf("suite %-18s rc=%d expected=%d %s%s%s\n", "strict", rc,
                   SQLITE_CONSTRAINT,
                   rc == SQLITE_CONSTRAINT ? "PASS" : "FAIL",
                   error ? " error=" : "", error ? error : "");
            if (error) sqlite3_free(error);
            error = 0;
            rc = rc == SQLITE_CONSTRAINT ? SQLITE_OK : SQLITE_ERROR;
        }
    }
    if (rc == SQLITE_OK && version >= 3038000)
        rc = expectValue(db, "json",
            "SELECT json_extract('{\"mvs\":370}','$.mvs');", "370");
    if (rc == SQLITE_OK && version >= 3039000)
        rc = expectValue(db, "right-join",
            "SELECT group_concat(x,'') FROM (SELECT coalesce(a.id,b.id) x "
            "FROM (SELECT 1 id UNION ALL SELECT 2) a RIGHT JOIN "
            "(SELECT 2 id UNION ALL SELECT 3) b USING(id) ORDER BY x);",
            "23");
    if (rc == SQLITE_OK && version >= 3045000)
        rc = expectValue(db, "jsonb",
            "SELECT json_extract(jsonb('{\"mvs\":534}'),'$.mvs');", "534");
    if (rc == SQLITE_OK && version >= 3046000)
        rc = expectValue(db, "numeric-underscore", "SELECT 1_234;", "1234");
    if (rc == SQLITE_OK && version >= 3048000)
        rc = expectValue(db, "two-arg-iif", "SELECT iif(1,'yes');", "yes");
    if (rc == SQLITE_OK && version >= 3049000)
        rc = expectValue(db, "variadic-iif",
            "SELECT iif(0,'no',0,'no',1,'yes','else');", "yes");
    if (rc == SQLITE_OK && version >= 3050000)
        rc = expectValue(db, "unistr", "SELECT length(unistr('\\u0041'));", "1");
    if (rc == SQLITE_OK && version >= 3053000)
        rc = expectValue(db, "json-array-insert",
            "SELECT json_array_insert('[1,3]','$[1]',2);", "[1,2,3]");
    if (rc == SQLITE_OK && version >= 3024000)
        rc = suiteSql(db, "modern-cleanup", "DROP TABLE ts_modern;");
    return rc;
}

static int runSuite(sqlite3 *db)
{
    char *error = 0;
    int rc;
    rc = suiteSql(db, "setup",
        "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; "
        "PRAGMA foreign_keys=ON; "
        "DROP VIEW IF EXISTS ts_view; DROP TABLE IF EXISTS ts_child; "
        "DROP TABLE IF EXISTS ts_parent; "
        "CREATE TABLE ts_parent(id INTEGER PRIMARY KEY,name TEXT UNIQUE); "
        "CREATE TABLE ts_child(id INTEGER PRIMARY KEY,parent_id INTEGER,"
        "amount INTEGER,note TEXT,FOREIGN KEY(parent_id) REFERENCES "
        "ts_parent(id) ON DELETE CASCADE); "
        "CREATE INDEX ts_child_parent ON ts_child(parent_id);");
    if (rc == SQLITE_OK) rc = suiteSql(db, "insert",
        "BEGIN; INSERT INTO ts_parent VALUES(1,'alpha'); "
        "INSERT INTO ts_parent VALUES(2,'beta'); "
        "INSERT INTO ts_child VALUES(1,1,10,'one'); "
        "INSERT INTO ts_child VALUES(2,1,20,NULL); "
        "INSERT INTO ts_child VALUES(3,2,30,'three'); COMMIT;");
    if (rc == SQLITE_OK) rc = expectValue(db, "join", "SELECT count(*) "
        "FROM ts_parent p JOIN ts_child c ON c.parent_id=p.id;", "3");
    if (rc == SQLITE_OK) rc = expectValue(db, "aggregate",
        "SELECT sum(amount) FROM ts_child;", "60");
    if (rc == SQLITE_OK) rc = expectValue(db, "null",
        "SELECT count(*) FROM ts_child WHERE note IS NULL;", "1");
    if (rc == SQLITE_OK) rc = suiteSql(db, "update",
        "UPDATE ts_parent SET name=upper(name) WHERE id=2;");
    if (rc == SQLITE_OK) rc = expectValue(db, "updated-value",
        "SELECT name FROM ts_parent WHERE id=2;", "BETA");
    if (rc == SQLITE_OK) rc = suiteSql(db, "rollback",
        "BEGIN; INSERT INTO ts_parent VALUES(9,'rollback'); ROLLBACK;");
    if (rc == SQLITE_OK) rc = expectValue(db, "rollback-check",
        "SELECT count(*) FROM ts_parent WHERE id=9;", "0");
    if (rc == SQLITE_OK) rc = suiteSql(db, "savepoint",
        "BEGIN; SAVEPOINT s1; INSERT INTO ts_parent VALUES(8,'savepoint'); "
        "ROLLBACK TO s1; RELEASE s1; COMMIT;");
    if (rc == SQLITE_OK) rc = expectValue(db, "savepoint-check",
        "SELECT count(*) FROM ts_parent WHERE id=8;", "0");
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(db, "INSERT INTO ts_parent VALUES(3,'alpha');",
                          0, 0, &error);
        printf("suite %-18s rc=%d expected=%d %s%s%s\n", "constraint", rc,
               SQLITE_CONSTRAINT, rc == SQLITE_CONSTRAINT ? "PASS" : "FAIL",
               error ? " error=" : "", error ? error : "");
        if (error) sqlite3_free(error);
        error = 0;
        rc = rc == SQLITE_CONSTRAINT ? SQLITE_OK : SQLITE_ERROR;
    }
    if (rc == SQLITE_OK) rc = expectValue(db, "blob",
        "SELECT length(zeroblob(4097));", "4097");
    if (rc == SQLITE_OK) rc = suiteSql(db, "view",
        "CREATE VIEW ts_view AS SELECT parent_id,sum(amount) total "
        "FROM ts_child GROUP BY parent_id;");
    if (rc == SQLITE_OK) rc = expectValue(db, "view-check",
        "SELECT total FROM ts_view WHERE parent_id=1;", "30");
    if (rc == SQLITE_OK) rc = suiteSql(db, "cascade",
        "DELETE FROM ts_parent WHERE id=1;");
    if (rc == SQLITE_OK) rc = expectValue(db, "cascade-check",
        "SELECT count(*) FROM ts_child;", "1");
    if (rc == SQLITE_OK) rc = suiteSql(db, "cleanup",
        "DROP VIEW ts_view; DROP TABLE ts_child; DROP TABLE ts_parent;");
    if (rc == SQLITE_OK) rc = suiteSql(db, "analyze", "ANALYZE;");
    if (rc == SQLITE_OK) rc = expectValue(db, "integrity",
        "PRAGMA integrity_check;", "ok");
    if (rc == SQLITE_OK) rc = suiteSql(db, "foreign-key-check",
        "PRAGMA foreign_key_check;");
    if (rc == SQLITE_OK) rc = runModernSuite(db);
    printf("SQLITE MVS TEST SUITE %s\n", rc == SQLITE_OK ? "PASSED" : "FAILED");
    return rc;
}

static int runSeedTestdb(sqlite3 *db)
{
    static const char *cities[] = {
        "ZAGREB", "SPLIT", "RIJEKA", "OSIJEK", "PULA"
    };
    static const char *names[] = {
        "ANA HORVAT", "IVAN KOVAC", "EMA BABIC", "LUKA MARIC",
        "IVA NOVAK", "MARKO ILIC", "SARA JURIC", "NIKO BASIC",
        "MIA PERIC", "LEA BOZIC", "TONI SARIC", "LANA VIDIC",
        "JOSIP ZEC", "PETRA KNEZ", "FILIP VUK", "DORA GRGIC",
        "ANTE ROGIC", "NINA POLIC", "MATE RAKIC", "MARIO KOS"
    };
    static const char *emails[] = {
        "ANA.HORVAT@EXAMPLE", "IVAN.KOVAC@EXAMPLE",
        "EMA.BABIC@EXAMPLE", "LUKA.MARIC@EXAMPLE",
        "IVA.NOVAK@EXAMPLE", "MARKO.ILIC@EXAMPLE",
        "SARA.JURIC@EXAMPLE", "NIKO.BASIC@EXAMPLE",
        "MIA.PERIC@EXAMPLE", "LEA.BOZIC@EXAMPLE",
        "TONI.SARIC@EXAMPLE", "LANA.VIDIC@EXAMPLE",
        "JOSIP.ZEC@EXAMPLE", "PETRA.KNEZ@EXAMPLE",
        "FILIP.VUK@EXAMPLE", "DORA.GRGIC@EXAMPLE",
        "ANTE.ROGIC@EXAMPLE", "NINA.POLIC@EXAMPLE",
        "MATE.RAKIC@EXAMPLE", "MARIO.KOS@EXAMPLE"
    };
    static const char *items[] = { "BOOK", "PEN", "MUG" };
    char sql[256];
    int person;
    int item;
    int rc;
    rc = suiteSql(db, "testdb-schema",
        "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; "
        "PRAGMA foreign_keys=ON; DROP TABLE IF EXISTS orders; "
        "DROP TABLE IF EXISTS people; "
        "CREATE TABLE people(id INTEGER PRIMARY KEY,name TEXT NOT NULL,"
        "city TEXT,age INTEGER,email TEXT UNIQUE); "
        "CREATE TABLE orders(id INTEGER PRIMARY KEY,people_id INTEGER "
        "NOT NULL,item TEXT NOT NULL,quantity INTEGER NOT NULL,amount INTEGER "
        "NOT NULL,FOREIGN KEY(people_id) REFERENCES people(id)); "
        "CREATE INDEX orders_people ON orders(people_id); BEGIN;");
    for (person = 1; rc == SQLITE_OK && person <= 20; person++) {
        snprintf(sql, sizeof(sql),
            "INSERT INTO people VALUES(%d,'%s','%s',%d,'%s');",
            person, names[person - 1], cities[(person - 1) % 5],
            person + 20, emails[person - 1]);
        rc = sqlite3_exec(db, sql, 0, 0, 0);
        for (item = 0; rc == SQLITE_OK && item < 3; item++) {
            snprintf(sql, sizeof(sql),
                "INSERT INTO orders VALUES(%d,%d,'%s',%d,%d);",
                person * 100 + item + 1, person, items[item], item + 1,
                person * 100 + (item + 1) * 25);
            rc = sqlite3_exec(db, sql, 0, 0, 0);
        }
    }
    if (rc == SQLITE_OK) rc = sqlite3_exec(db, "COMMIT;", 0, 0, 0);
    else sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
    printf("testdb seed rc=%d people=20 orders=60\n", rc);
    if (rc == SQLITE_OK) rc = expectValue(db, "testdb-people",
        "SELECT count(*) FROM people;", "20");
    if (rc == SQLITE_OK) rc = expectValue(db, "testdb-orders",
        "SELECT count(*) FROM orders;", "60");
    if (rc == SQLITE_OK) rc = expectValue(db, "testdb-three-each",
        "SELECT count(*) FROM (SELECT people_id FROM orders GROUP BY "
        "people_id HAVING count(*)=3);", "20");
    if (rc == SQLITE_OK) rc = expectValue(db, "testdb-first-name",
        "SELECT name FROM people WHERE id=1;", "ANA HORVAT");
    if (rc == SQLITE_OK) rc = expectValue(db, "testdb-last-name",
        "SELECT name FROM people WHERE id=20;", "MARIO KOS");
    printf("SQLITE TESTDB %s\n", rc == SQLITE_OK ? "CREATED" : "FAILED");
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
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "WAIT") == 0)
        rc = runWaitTest(db);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "CRASH") == 0)
        rc = runCrashTest(db);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "SUITE") == 0)
        rc = runSuite(db);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "SEED") == 0)
        rc = runSeedTestdb(db);
    else if (rc == SQLITE_OK && argc > 1 && strcmp(argv[1], "BACKUP") == 0)
        rc = runBackupTest(db);
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
