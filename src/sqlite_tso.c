#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

#define TSO_LINE 2048
#define TSO_OUT 512

extern int tsqtget(char *buf, int max) asm("TSQTGET");
extern int tsqtput(char *buf, int len) asm("TSQTPUT");

typedef struct RowOutput RowOutput;
struct RowOutput { int header; };

static void appendText(char *line, int size, int *used, const char *text)
{
    int available;
    int added;
    if (*used >= size - 1) return;
    available = size - *used;
    added = snprintf(line + *used, available, "%s", text);
    if (added < 0) return;
    if (added >= available)
        *used = size - 1;
    else
        *used += added;
}

static void tsoOut(const char *format, ...)
{
    char output[TSO_OUT];
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(output, sizeof(output), format, args);
    va_end(args);
    if (length < 0) return;
    if (length >= (int)sizeof(output)) length = sizeof(output) - 1;
    tsqtput(output, length);
}

static int printRow(void *context, int columns, char **values, char **names)
{
    RowOutput *output = (RowOutput *)context;
    char line[TSO_OUT];
    int used = 0;
    int i;
    if (!output->header) {
        line[0] = '\0';
        for (i = 0; i < columns; i++) {
            if (i) appendText(line, sizeof(line), &used, " | ");
            appendText(line, sizeof(line), &used, names[i]);
        }
        tsoOut("%s", line);
        output->header = 1;
    }
    used = 0;
    line[0] = '\0';
    for (i = 0; i < columns; i++) {
        if (i) appendText(line, sizeof(line), &used, " | ");
        appendText(line, sizeof(line), &used,
                   values[i] ? values[i] : "NULL");
    }
    tsoOut("%s", line);
    return 0;
}

static char *trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t') text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
           end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
    return text;
}

static int executeSql(sqlite3 *db, const char *sql)
{
    RowOutput output;
    char *error = 0;
    int rc;
    output.header = 0;
    rc = sqlite3_exec(db, sql, printRow, &output, &error);
    if (rc == SQLITE_OK)
        tsoOut(output.header ? "OK" : "OK (%d changes)", sqlite3_changes(db));
    else
        tsoOut("ERROR %d: %s", rc, error ? error : sqlite3_errmsg(db));
    if (error) sqlite3_free(error);
    return rc;
}

static int dotCommand(sqlite3 *db, char *line)
{
    if (strcmp(line, ".quit") == 0 || strcmp(line, ".exit") == 0)
        return 1;
    if (strcmp(line, ".help") == 0) {
        tsoOut("Enter SQL terminated by ;");
        tsoOut(".tables  .schema [table]  .quit");
    } else if (strcmp(line, ".tables") == 0) {
        executeSql(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    } else if (strncmp(line, ".schema", 7) == 0) {
        char sql[TSO_OUT];
        char *name = trim(line + 7);
        if (*name)
            snprintf(sql, sizeof(sql),
                "SELECT sql FROM sqlite_master WHERE name='%s';", name);
        else
            strcpy(sql, "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY name;");
        executeSql(db, sql);
    } else {
        tsoOut("Unknown command. Use .help");
    }
    return 0;
}

int main(void)
{
    sqlite3 *db = 0;
    char line[TSO_LINE];
    char statement[TSO_LINE];
    int length;
    int rc;
    statement[0] = '\0';
    rc = sqlite3_initialize();
    if (rc == SQLITE_OK)
        rc = sqlite3_open_v2("SQLDB", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    if (rc != SQLITE_OK) {
        tsoOut("Cannot open SQLite database: %s",
               db ? sqlite3_errmsg(db) : "initialization failed");
        if (db) sqlite3_close(db);
        return 8;
    }
    executeSql(db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;");
    tsoOut("SQLite %s for MVS TSO", sqlite3_libversion());
    tsoOut("Use .help for commands");
    for (;;) {
        tsoOut(statement[0] ? "   ...> " : "sqlite> ");
        memset(line, 0, sizeof(line));
        length = tsqtget(line, sizeof(line));
        if (length < 0) break;
        if (length >= (int)sizeof(line)) length = sizeof(line) - 1;
        line[length] = '\0';
        if (statement[0] == '\0' && trim(line)[0] == '.') {
            if (dotCommand(db, trim(line))) break;
            continue;
        }
        if ((int)strlen(statement) + (int)strlen(trim(line)) + 2 >= TSO_LINE) {
            tsoOut("ERROR: statement too long");
            statement[0] = '\0';
            continue;
        }
        if (statement[0]) strcat(statement, " ");
        strcat(statement, trim(line));
        if (sqlite3_complete(statement)) {
            executeSql(db, statement);
            statement[0] = '\0';
        }
    }
    sqlite3_close(db);
    sqlite3_shutdown();
    tsoOut("SQLite TSO session ended");
    return 0;
}
