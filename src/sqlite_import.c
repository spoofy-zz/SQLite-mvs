/* Batch SQL importer for the MVS RRDS VFS. Input is an FB text DD SQLIN. */
#include <stdio.h>
#include <string.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

#define IMPORT_LINE 1024
#define IMPORT_STATEMENT 8192

static void trimRight(char *text)
{
    int length = (int)strlen(text);
    while (length > 0 && (text[length - 1] == ' ' ||
           text[length - 1] == '\n' || text[length - 1] == '\r' ||
           text[length - 1] == '\t'))
        text[--length] = '\0';
}

int main(int argc, char **argv)
{
    const char *databaseName = argc > 1 ? argv[1] : "SQLDB";
    sqlite3 *db = 0;
    FILE *input = 0;
    char line[IMPORT_LINE];
    char statement[IMPORT_STATEMENT];
    char *error = 0;
    int statements = 0;
    int lines = 0;
    int rc;
    int closeRc;

    statement[0] = '\0';
    rc = sqlite3_initialize();
    if (rc == SQLITE_OK)
        rc = sqlite3_open_v2(databaseName, &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    if (rc != SQLITE_OK) {
        printf("SQL import cannot open database: %s\n",
               db ? sqlite3_errmsg(db) : "initialization failed");
        goto done;
    }
    sqlite3_busy_timeout(db, 30000);
    input = fopen("dd:SQLIN", "r");
    if (!input) {
        printf("SQL import cannot open DD SQLIN\n");
        rc = SQLITE_CANTOPEN;
        goto done;
    }

    while (fgets(line, sizeof(line), input)) {
        int used;
        int added;
        lines++;
        trimRight(line);
        if (!line[0] || (line[0] == '-' && line[1] == '-')) continue;
        used = (int)strlen(statement);
        added = (int)strlen(line);
        if (used + added + 2 >= IMPORT_STATEMENT) {
            printf("SQL import statement too long near input line %d\n", lines);
            rc = SQLITE_TOOBIG;
            goto failed;
        }
        if (used) statement[used++] = ' ';
        memcpy(statement + used, line, (size_t)added + 1);
        if (!sqlite3_complete(statement)) continue;
        rc = sqlite3_exec(db, statement, 0, 0, &error);
        statements++;
        if (rc != SQLITE_OK) {
            printf("SQL import failed at statement %d, input line %d: %s\n",
                   statements, lines, error ? error : sqlite3_errmsg(db));
            printf("SQL: %.120s\n", statement);
            goto failed;
        }
        statement[0] = '\0';
    }
    if (ferror(input)) {
        printf("SQL import read error after input line %d\n", lines);
        rc = SQLITE_IOERR_READ;
        goto failed;
    }
    if (statement[0]) {
        printf("SQL import has incomplete final statement\n");
        rc = SQLITE_ERROR;
        goto failed;
    }
    printf("SQL import complete: lines=%d statements=%d changes=%d\n",
           lines, statements, sqlite3_total_changes(db));
    rc = SQLITE_OK;
    goto done;

failed:
    if (db) sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
done:
    if (error) sqlite3_free(error);
    if (input) fclose(input);
    if (db) {
        closeRc = sqlite3_close(db);
        if (rc == SQLITE_OK && closeRc != SQLITE_OK) rc = closeRc;
    }
    sqlite3_shutdown();
    return rc == SQLITE_OK ? 0 : 8;
}
