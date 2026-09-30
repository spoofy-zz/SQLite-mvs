#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

#define TSO_LINE 2048
#define TSO_OUT 2048
#define TERM_ENTER 0x7d
#define TERM_CLEAR 0x6d
#define TERM_PF3   0xf3
#define MODE_COLUMN 0
#define MODE_LIST   1
#define MODE_LINE   2
#define MODE_CSV    3
#define COLUMN_WIDTH 18
#define MAX_COLUMNS 16

extern int tsqtget(char *buf, int max) asm("TSQTGET");
extern int tsqtput(char *buf, int len) asm("TSQTPUT");
extern int tsqtclr(void) asm("TSQTCLR");

typedef struct ShellState ShellState;
typedef struct RowOutput RowOutput;
static int dotCommand(sqlite3 *db, ShellState *shell, char *line);
static int runScript(sqlite3 *db, ShellState *shell, FILE *input);
static FILE *shellOutput = 0;
static int shellOutputOnce = 0;
struct ShellState {
    int headers;
    int mode;
    int echo;
    int changes;
    int timer;
    int timeout;
    int widths[MAX_COLUMNS];
    char nullValue[32];
    char separator[16];
};
struct RowOutput {
    int header;
    int rows;
    ShellState *shell;
};

static int termAddress(unsigned char first, unsigned char second)
{
    return (first & 0xc0) ? ((first & 63) * 64 + (second & 63))
                          : ((first & 63) * 256 + second);
}

/* TGET ASIS returns a 3270 Read Modified record, not a plain C string:
 * AID, cursor address, then one or more SBA/address/text groups. */
static int termInput(char *out, int capacity, const unsigned char *raw,
                     int length)
{
    int i;
    int position = -1;
    int used = 0;
    if (length < 3 || raw[0] != TERM_ENTER) return -1;
    for (i = 3; i < length; i++) {
        if (raw[i] == 0x11) {
            if (i + 2 >= length) return -1;
            position = termAddress(raw[i + 1], raw[i + 2]);
            i += 2;
            if (used && out[used - 1] != ' ') {
                if (used >= capacity - 1) return -1;
                out[used++] = ' ';
            }
        } else {
            if (position < 0 || used >= capacity - 1) return -1;
            out[used++] = raw[i] ? (char)raw[i] : ' ';
            position++;
        }
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
    return used;
}

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

static void appendColumn(char *line, int size, int *used, const char *text,
                         int width)
{
    char field[81];
    int length = (int)strlen(text);
    int i;
    if (width < 1) width = COLUMN_WIDTH;
    if (width > 80) width = 80;
    if (length > width) length = width;
    memcpy(field, text, length);
    for (i = length; i < width; i++) field[i] = ' ';
    field[width] = '\0';
    appendText(line, size, used, field);
}

static void appendRule(char *line, int size, int *used, int width)
{
    char rule[81];
    int i;
    if (width < 1) width = COLUMN_WIDTH;
    if (width > 80) width = 80;
    for (i = 0; i < width; i++) rule[i] = '-';
    rule[width] = '\0';
    appendText(line, size, used, rule);
}

static void appendCsv(char *line, int size, int *used, const char *text)
{
    const char *p;
    int quote = 0;
    for (p = text; *p; p++)
        if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') quote = 1;
    if (quote) appendText(line, size, used, "\"");
    for (p = text; *p; p++) {
        char one[2];
        one[0] = *p;
        one[1] = '\0';
        if (*p == '"') appendText(line, size, used, "\"\"");
        else appendText(line, size, used, one);
    }
    if (quote) appendText(line, size, used, "\"");
}

static int equalIgnoreCase(const char *left, const char *right)
{
    while (*left && *right) {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right))
            return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int startsIgnoreCase(const char *text, const char *prefix)
{
    while (*prefix) {
        if (!*text || toupper((unsigned char)*text) !=
            toupper((unsigned char)*prefix)) return 0;
        text++;
        prefix++;
    }
    return 1;
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
    if (shellOutput) {
        fwrite(output, 1, length, shellOutput);
        fputc('\n', shellOutput);
    } else {
        tsqtput(output, length);
    }
}

static void terminalOut(const char *format, ...)
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

static void closeShellOutput(void)
{
    if (shellOutput) fclose(shellOutput);
    shellOutput = 0;
    shellOutputOnce = 0;
}

static int printRow(void *context, int columns, char **values, char **names)
{
    RowOutput *output = (RowOutput *)context;
    char line[TSO_OUT];
    char rule[TSO_OUT];
    int used = 0;
    int ruleUsed = 0;
    int i;
    int width;
    if (output->shell->mode == MODE_LINE) {
        int nameWidth = 0;
        if (output->rows) tsoOut("");
        for (i = 0; i < columns; i++) {
            width = (int)strlen(names[i]);
            if (width > nameWidth) nameWidth = width;
        }
        for (i = 0; i < columns; i++)
            tsoOut("%*s = %s", nameWidth, names[i],
                   values[i] ? values[i] : output->shell->nullValue);
        output->header = 1;
        output->rows++;
        return 0;
    }
    if (!output->header && output->shell->headers) {
        line[0] = '\0';
        rule[0] = '\0';
        for (i = 0; i < columns; i++) {
            if (output->shell->mode == MODE_COLUMN) {
                width = i < MAX_COLUMNS ? output->shell->widths[i]
                                        : COLUMN_WIDTH;
                if (i) {
                    appendText(line, sizeof(line), &used, "  ");
                    appendText(rule, sizeof(rule), &ruleUsed, "  ");
                }
                if (i == columns - 1)
                    appendText(line, sizeof(line), &used, names[i]);
                else
                    appendColumn(line, sizeof(line), &used, names[i], width);
                appendRule(rule, sizeof(rule), &ruleUsed, width);
            } else if (output->shell->mode == MODE_LIST) {
                int n;
                if (i) {
                    appendText(line, sizeof(line), &used, " | ");
                    appendText(rule, sizeof(rule), &ruleUsed, "-+-");
                }
                appendText(line, sizeof(line), &used, names[i]);
                for (n = 0; names[i][n]; n++)
                    appendText(rule, sizeof(rule), &ruleUsed, "-");
            } else {
                if (i) appendText(line, sizeof(line), &used, ",");
                appendCsv(line, sizeof(line), &used, names[i]);
            }
        }
        tsoOut("%s", line);
        if (output->shell->mode != MODE_CSV) tsoOut("%s", rule);
        output->header = 1;
    }
    used = 0;
    line[0] = '\0';
    for (i = 0; i < columns; i++) {
        if (output->shell->mode == MODE_COLUMN) {
            width = i < MAX_COLUMNS ? output->shell->widths[i]
                                    : COLUMN_WIDTH;
            if (i) appendText(line, sizeof(line), &used, "  ");
            if (i == columns - 1)
                appendText(line, sizeof(line), &used,
                           values[i] ? values[i] : output->shell->nullValue);
            else
                appendColumn(line, sizeof(line), &used,
                             values[i] ? values[i] : output->shell->nullValue,
                             width);
        } else if (output->shell->mode == MODE_LIST) {
            if (i) appendText(line, sizeof(line), &used,
                              output->shell->separator);
            appendText(line, sizeof(line), &used,
                       values[i] ? values[i] : output->shell->nullValue);
        } else {
            if (i) appendText(line, sizeof(line), &used, ",");
            appendCsv(line, sizeof(line), &used,
                      values[i] ? values[i] : output->shell->nullValue);
        }
    }
    tsoOut("%s", line);
    output->rows++;
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

static int executeSql(sqlite3 *db, ShellState *shell, const char *sql)
{
    RowOutput output;
    char *error = 0;
    int rc;
    clock_t started = clock();
    output.header = 0;
    output.rows = 0;
    output.shell = shell;
    if (shell->echo) tsoOut("SQL> %s", sql);
    rc = sqlite3_exec(db, sql, printRow, &output, &error);
    if (rc == SQLITE_OK) {
        if (output.rows)
            tsoOut("%d row%s", output.rows, output.rows == 1 ? "" : "s");
        else if (startsIgnoreCase(sql, "SELECT") ||
                 startsIgnoreCase(sql, "PRAGMA"))
            tsoOut("(no rows)");
        else
            tsoOut("OK (%d changes)", sqlite3_changes(db));
    }
    else
        tsoOut("ERROR %d: %s", rc, error ? error : sqlite3_errmsg(db));
    if (error) sqlite3_free(error);
    if (rc == SQLITE_OK && shell->changes)
        tsoOut("Changes: %d  Total changes: %d", sqlite3_changes(db),
               sqlite3_total_changes(db));
    if (shell->timer)
        tsoOut("CPU time: %.3f seconds",
               (double)(clock() - started) / (double)CLOCKS_PER_SEC);
    if (shellOutputOnce) closeShellOutput();
    return rc;
}

static void resetShell(ShellState *shell)
{
    int i;
    shell->headers = 1;
    shell->mode = MODE_COLUMN;
    shell->echo = 0;
    shell->changes = 0;
    shell->timer = 0;
    shell->timeout = 0;
    strcpy(shell->nullValue, "NULL");
    strcpy(shell->separator, " | ");
    for (i = 0; i < MAX_COLUMNS; i++) shell->widths[i] = COLUMN_WIDTH;
}

static char *sqlQuote(const char *value)
{
    return sqlite3_mprintf("%q", value);
}

static int ddPath(const char *argument, char *path, int capacity)
{
    const char *name = argument;
    int i;
    int length;
    if (startsIgnoreCase(name, "DD:")) name += 3;
    length = (int)strlen(name);
    if (length < 1 || length > 8) return 0;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '@' || c == '#' || c == '$')) return 0;
    }
    snprintf(path, capacity, "DD:%s", name);
    return 1;
}

static void traceSql(void *unused, const char *sql)
{
    (void)unused;
    tsoOut("TRACE> %s", sql);
}

static void dumpValue(char *line, int size, int *used, sqlite3_stmt *row,
                      int column)
{
    int type = sqlite3_column_type(row, column);
    char *value = 0;
    if (type == SQLITE_NULL) {
        appendText(line, size, used, "NULL");
    } else if (type == SQLITE_INTEGER) {
        value = sqlite3_mprintf("%lld", sqlite3_column_int64(row, column));
    } else if (type == SQLITE_FLOAT) {
        value = sqlite3_mprintf("%!.15g", sqlite3_column_double(row, column));
    } else if (type == SQLITE_BLOB) {
        const unsigned char *blob = sqlite3_column_blob(row, column);
        int bytes = sqlite3_column_bytes(row, column);
        int i;
        appendText(line, size, used, "X'");
        for (i = 0; i < bytes && *used < size - 3; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", blob[i]);
            appendText(line, size, used, hex);
        }
        appendText(line, size, used, "'");
    } else {
        value = sqlite3_mprintf("%Q", sqlite3_column_text(row, column));
    }
    if (value) {
        appendText(line, size, used, value);
        sqlite3_free(value);
    }
}

static int dumpTable(sqlite3 *db, const char *name)
{
    sqlite3_stmt *row = 0;
    char *quoted = sqlite3_mprintf("\"%w\"", name);
    char *sql;
    int rc;
    if (!quoted) return SQLITE_NOMEM;
    sql = sqlite3_mprintf("SELECT * FROM %s;", quoted);
    if (!sql) {
        sqlite3_free(quoted);
        return SQLITE_NOMEM;
    }
    rc = sqlite3_prepare_v2(db, sql, -1, &row, 0);
    sqlite3_free(sql);
    while (rc == SQLITE_OK && (rc = sqlite3_step(row)) == SQLITE_ROW) {
        char line[TSO_LINE];
        int used = 0;
        int i;
        int columns = sqlite3_column_count(row);
        line[0] = '\0';
        appendText(line, sizeof(line), &used, "INSERT INTO ");
        appendText(line, sizeof(line), &used, quoted);
        appendText(line, sizeof(line), &used, " VALUES(");
        for (i = 0; i < columns; i++) {
            if (i) appendText(line, sizeof(line), &used, ",");
            dumpValue(line, sizeof(line), &used, row, i);
        }
        appendText(line, sizeof(line), &used, ");");
        tsoOut("%s", line);
    }
    sqlite3_finalize(row);
    sqlite3_free(quoted);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

static int dumpDatabase(sqlite3 *db, const char *onlyTable)
{
    sqlite3_stmt *master = 0;
    int rc;
    rc = sqlite3_prepare_v2(db,
        "SELECT name,sql FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%' AND (?1='' OR name=?1) "
        "ORDER BY name;", -1, &master, 0);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_text(master, 1, onlyTable, -1, SQLITE_TRANSIENT);
    tsoOut("PRAGMA foreign_keys=OFF;");
    tsoOut("BEGIN TRANSACTION;");
    while ((rc = sqlite3_step(master)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(master, 0);
        const char *schema = (const char *)sqlite3_column_text(master, 1);
        if (schema) tsoOut("%s;", schema);
        rc = dumpTable(db, name);
        if (rc != SQLITE_OK) break;
    }
    sqlite3_finalize(master);
    if (rc == SQLITE_DONE) {
        rc = sqlite3_prepare_v2(db,
            "SELECT sql FROM sqlite_master WHERE type IN "
            "('index','trigger','view') AND sql IS NOT NULL "
            "AND (?1='' OR tbl_name=?1) ORDER BY type,name;",
            -1, &master, 0);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(master, 1, onlyTable, -1, SQLITE_TRANSIENT);
            while ((rc = sqlite3_step(master)) == SQLITE_ROW)
                tsoOut("%s;", sqlite3_column_text(master, 0));
            sqlite3_finalize(master);
        }
    }
    if (rc == SQLITE_DONE) {
        tsoOut("COMMIT;");
        return SQLITE_OK;
    }
    tsoOut("ROLLBACK;");
    return rc;
}

static int dotCommand(sqlite3 *db, ShellState *shell, char *line)
{
    if (equalIgnoreCase(line, ".quit") || equalIgnoreCase(line, ".exit"))
        return 1;
    if (equalIgnoreCase(line, ".help")) {
        tsoOut("Command             Description");
        tsoOut("------------------  --------------------------------");
        tsoOut(".tables             List tables");
        tsoOut(".indexes [table]    List indexes");
        tsoOut(".schema [table]     Show CREATE statements");
        tsoOut(".tableinfo table    Show table columns");
        tsoOut(".databases          List attached databases");
        tsoOut(".foreignkeys on|off Enable or disable FK checks");
        tsoOut(".stats              Show database statistics");
        tsoOut(".integrity_check    Run database integrity check");
        tsoOut(".foreign_key_check  Find foreign-key violations");
        tsoOut(".analyze            Refresh query planner statistics");
        tsoOut(".vacuum             Rebuild and compact the database");
        tsoOut(".lastid             Show last inserted rowid");
        tsoOut(".headers on|off     Show or hide column headers");
        tsoOut(".mode column|list|line|csv  Select output format");
        tsoOut(".separator TEXT     Set list-mode separator");
        tsoOut(".width N ...        Set column widths (1-80)");
        tsoOut(".nullvalue TEXT     Set NULL display text");
        tsoOut(".echo on|off        Echo SQL before execution");
        tsoOut(".changes on|off     Show per-statement change counts");
        tsoOut(".timer on|off       Show SQL CPU time");
        tsoOut(".trace on|off       Show statements executed by SQLite");
        tsoOut(".timeout MS         Wait for locks (0 disables)");
        tsoOut(".show               Show shell settings");
        tsoOut(".reset              Restore default shell settings");
        tsoOut(".read DDNAME        Execute SQL from an allocated DD");
        tsoOut(".output DDNAME      Redirect output (.output terminal)");
        tsoOut(".once DDNAME        Redirect the next SQL result");
        tsoOut(".dump [table]       Write database as SQL text");
        tsoOut(".clear              Clear screen and move cursor home");
        tsoOut(".version            Show SQLite version");
        tsoOut(".quit / .exit       Return to TSO READY");
        tsoOut("SQL statements must end with ;  PF3 exits");
    } else if (equalIgnoreCase(line, ".tables")) {
        executeSql(db, shell,
          "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    } else if (startsIgnoreCase(line, ".indexes")) {
        char sql[TSO_OUT];
        char *name = trim(line + 8);
        char *quoted = sqlQuote(name);
        if (!quoted) {
            tsoOut("ERROR: insufficient memory");
        } else {
            if (*name)
                snprintf(sql, sizeof(sql),
                    "SELECT name FROM sqlite_master WHERE type='index' "
                    "AND tbl_name='%s' ORDER BY name;", quoted);
            else
                strcpy(sql, "SELECT name FROM sqlite_master WHERE "
                       "type='index' ORDER BY name;");
            sqlite3_free(quoted);
            executeSql(db, shell, sql);
        }
    } else if (startsIgnoreCase(line, ".schema")) {
        char sql[TSO_OUT];
        char *name = trim(line + 7);
        char *quoted = sqlQuote(name);
        if (!quoted) {
            tsoOut("ERROR: insufficient memory");
        } else if (*name) {
            snprintf(sql, sizeof(sql),
                "SELECT sql FROM sqlite_master WHERE name='%s';", quoted);
            sqlite3_free(quoted);
            executeSql(db, shell, sql);
        } else {
            sqlite3_free(quoted);
            strcpy(sql, "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY name;");
            executeSql(db, shell, sql);
        }
    } else if (startsIgnoreCase(line, ".tableinfo")) {
        char sql[TSO_OUT];
        char *name = trim(line + 10);
        char *quoted;
        if (!*name) {
            tsoOut("Usage: .tableinfo TABLE");
        } else {
            quoted = sqlQuote(name);
            if (!quoted) tsoOut("ERROR: insufficient memory");
            else {
                snprintf(sql, sizeof(sql), "PRAGMA table_info('%s');",
                         quoted);
                sqlite3_free(quoted);
                executeSql(db, shell, sql);
            }
        }
    } else if (equalIgnoreCase(line, ".databases")) {
        executeSql(db, shell, "PRAGMA database_list;");
    } else if (startsIgnoreCase(line, ".foreignkeys")) {
        char *value = trim(line + 12);
        if (equalIgnoreCase(value, "on")) {
            executeSql(db, shell, "PRAGMA foreign_keys=ON;");
            tsoOut("Foreign keys: on");
        } else if (equalIgnoreCase(value, "off")) {
            executeSql(db, shell, "PRAGMA foreign_keys=OFF;");
            tsoOut("Foreign keys: off");
        } else if (!*value) {
            executeSql(db, shell, "PRAGMA foreign_keys;");
        } else tsoOut("Usage: .foreignkeys on|off");
    } else if (equalIgnoreCase(line, ".stats")) {
        executeSql(db, shell, "PRAGMA page_size;");
        executeSql(db, shell, "PRAGMA page_count;");
        executeSql(db, shell, "PRAGMA freelist_count;");
        executeSql(db, shell, "PRAGMA journal_mode;");
        executeSql(db, shell, "PRAGMA synchronous;");
    } else if (equalIgnoreCase(line, ".integrity") ||
               equalIgnoreCase(line, ".integrity_check")) {
        executeSql(db, shell, "PRAGMA integrity_check;");
    } else if (equalIgnoreCase(line, ".fkcheck") ||
               equalIgnoreCase(line, ".foreign_key_check")) {
        executeSql(db, shell, "PRAGMA foreign_key_check;");
    } else if (equalIgnoreCase(line, ".analyze")) {
        executeSql(db, shell, "ANALYZE;");
    } else if (equalIgnoreCase(line, ".vacuum")) {
        tsoOut("UNSUPPORTED: VACUUM needs a second RRDS temporary database");
        tsoOut("Use auto_vacuum=FULL; raw RRDS aliasing is intentionally blocked");
    } else if (equalIgnoreCase(line, ".lastid")) {
        tsoOut("Last insert rowid");
        tsoOut("-----------------");
        tsoOut("%lld", sqlite3_last_insert_rowid(db));
    } else if (equalIgnoreCase(line, ".version")) {
        tsoOut("SQLite version");
        tsoOut("------------------");
        tsoOut("%s (%d)", sqlite3_libversion(), sqlite3_libversion_number());
    } else if (startsIgnoreCase(line, ".headers")) {
        char *value = trim(line + 8);
        if (equalIgnoreCase(value, "on")) {
            shell->headers = 1;
            tsoOut("Headers: on");
        } else if (equalIgnoreCase(value, "off")) {
            shell->headers = 0;
            tsoOut("Headers: off");
        }
        else tsoOut("Usage: .headers on|off");
    } else if (startsIgnoreCase(line, ".mode")) {
        char *value = trim(line + 5);
        if (equalIgnoreCase(value, "column")) {
            shell->mode = MODE_COLUMN;
            tsoOut("Mode: column");
        } else if (equalIgnoreCase(value, "list")) {
            shell->mode = MODE_LIST;
            tsoOut("Mode: list");
        } else if (equalIgnoreCase(value, "line")) {
            shell->mode = MODE_LINE;
            tsoOut("Mode: line");
        } else if (equalIgnoreCase(value, "csv")) {
            shell->mode = MODE_CSV;
            tsoOut("Mode: csv");
        }
        else tsoOut("Usage: .mode column|list|line|csv");
    } else if (startsIgnoreCase(line, ".separator")) {
        char *value = trim(line + 10);
        if (!*value || strlen(value) >= sizeof(shell->separator))
            tsoOut("Usage: .separator TEXT (maximum 15 characters)");
        else {
            strcpy(shell->separator, value);
            tsoOut("Separator: %s", shell->separator);
        }
    } else if (startsIgnoreCase(line, ".width")) {
        char *value = trim(line + 6);
        char *part;
        int column = 0;
        part = strtok(value, " ");
        while (part && column < MAX_COLUMNS) {
            int width = atoi(part);
            if (width < 1 || width > 80) {
                tsoOut("Usage: .width N ... (each N is 1-80)");
                return 0;
            }
            shell->widths[column++] = width;
            part = strtok(0, " ");
        }
        if (!column) tsoOut("Usage: .width N ... (each N is 1-80)");
        else tsoOut("Widths updated for %d column%s", column,
                    column == 1 ? "" : "s");
    } else if (startsIgnoreCase(line, ".nullvalue")) {
        char *value = trim(line + 10);
        if (!*value || strlen(value) >= sizeof(shell->nullValue))
            tsoOut("Usage: .nullvalue TEXT (maximum 31 characters)");
        else {
            strcpy(shell->nullValue, value);
            tsoOut("NULL value: %s", shell->nullValue);
        }
    } else if (startsIgnoreCase(line, ".echo")) {
        char *value = trim(line + 5);
        if (equalIgnoreCase(value, "on")) shell->echo = 1;
        else if (equalIgnoreCase(value, "off")) shell->echo = 0;
        else {
            tsoOut("Usage: .echo on|off");
            return 0;
        }
        tsoOut("Echo: %s", shell->echo ? "on" : "off");
    } else if (startsIgnoreCase(line, ".changes")) {
        char *value = trim(line + 8);
        if (equalIgnoreCase(value, "on")) shell->changes = 1;
        else if (equalIgnoreCase(value, "off")) shell->changes = 0;
        else {
            tsoOut("Usage: .changes on|off");
            return 0;
        }
        tsoOut("Changes: %s", shell->changes ? "on" : "off");
    } else if (startsIgnoreCase(line, ".timer")) {
        char *value = trim(line + 6);
        if (equalIgnoreCase(value, "on")) shell->timer = 1;
        else if (equalIgnoreCase(value, "off")) shell->timer = 0;
        else {
            tsoOut("Usage: .timer on|off");
            return 0;
        }
        tsoOut("Timer: %s", shell->timer ? "on" : "off");
    } else if (startsIgnoreCase(line, ".trace")) {
        char *value = trim(line + 6);
        if (equalIgnoreCase(value, "on")) {
            sqlite3_trace(db, traceSql, 0);
            tsoOut("Trace: on");
        } else if (equalIgnoreCase(value, "off")) {
            sqlite3_trace(db, 0, 0);
            tsoOut("Trace: off");
        } else tsoOut("Usage: .trace on|off");
    } else if (startsIgnoreCase(line, ".timeout")) {
        char *value = trim(line + 8);
        int timeout = atoi(value);
        if (!*value || timeout < 0) tsoOut("Usage: .timeout MILLISECONDS");
        else {
            shell->timeout = timeout;
            sqlite3_busy_timeout(db, timeout);
            tsoOut("Busy timeout: %d ms", timeout);
        }
    } else if (equalIgnoreCase(line, ".show")) {
        tsoOut("Setting             Value");
        tsoOut("------------------  ------------------------------");
        tsoOut("headers             %s", shell->headers ? "on" : "off");
        tsoOut("mode                %s",
               shell->mode == MODE_COLUMN ? "column" :
               (shell->mode == MODE_LIST ? "list" :
               (shell->mode == MODE_LINE ? "line" : "csv")));
        tsoOut("echo                %s", shell->echo ? "on" : "off");
        tsoOut("changes             %s", shell->changes ? "on" : "off");
        tsoOut("timer               %s", shell->timer ? "on" : "off");
        tsoOut("timeout             %d ms", shell->timeout);
        tsoOut("nullvalue           %s", shell->nullValue);
        tsoOut("separator           %s", shell->separator);
    } else if (equalIgnoreCase(line, ".reset")) {
        resetShell(shell);
        sqlite3_busy_timeout(db, 0);
        sqlite3_trace(db, 0, 0);
        tsoOut("Shell settings restored to defaults");
    } else if (startsIgnoreCase(line, ".read")) {
        char path[16];
        char *name = trim(line + 5);
        FILE *input;
        if (!ddPath(name, path, sizeof(path))) {
            tsoOut("Usage: .read DDNAME (1-8 DD characters)");
        } else {
            input = fopen(path, "r");
            if (!input) tsoOut("ERROR: cannot open %s for input", path);
            else {
                runScript(db, shell, input);
                fclose(input);
            }
        }
    } else if (startsIgnoreCase(line, ".dump")) {
        char *name = trim(line + 5);
        int rc = dumpDatabase(db, name);
        if (rc != SQLITE_OK)
            tsoOut("ERROR %d while creating dump: %s", rc,
                   sqlite3_errmsg(db));
        if (shellOutputOnce) closeShellOutput();
    } else if (startsIgnoreCase(line, ".output") ||
               startsIgnoreCase(line, ".once")) {
        int once = startsIgnoreCase(line, ".once");
        char *name = trim(line + (once ? 5 : 7));
        char path[16];
        FILE *output;
        if (!once && equalIgnoreCase(name, "terminal")) {
            closeShellOutput();
            terminalOut("Output: terminal");
        } else if (!ddPath(name, path, sizeof(path))) {
            tsoOut("Usage: %s DDNAME", once ? ".once" : ".output");
        } else {
            output = fopen(path, "w");
            if (!output) tsoOut("ERROR: cannot open %s for output", path);
            else {
                closeShellOutput();
                shellOutput = output;
                shellOutputOnce = once;
                terminalOut("Output: %s%s", path, once ? " (once)" : "");
            }
        }
    } else if (equalIgnoreCase(line, ".clear")) {
        if (tsqtclr() != 0) tsoOut("ERROR: cannot clear terminal screen");
    } else {
        tsoOut("Unknown command. Use .help");
    }
    return 0;
}

static int runScript(sqlite3 *db, ShellState *shell, FILE *input)
{
    char line[TSO_LINE];
    char statement[TSO_LINE];
    char *text;
    int lineNumber = 0;
    statement[0] = '\0';
    while (fgets(line, sizeof(line), input)) {
        lineNumber++;
        text = trim(line);
        if (!*text || (text[0] == '-' && text[1] == '-')) continue;
        if (!statement[0] && text[0] == '.') {
            if (dotCommand(db, shell, text)) return 1;
            continue;
        }
        if ((int)strlen(statement) + (int)strlen(text) + 2 >= TSO_LINE) {
            tsoOut("ERROR: script statement too long near line %d",
                   lineNumber);
            statement[0] = '\0';
            continue;
        }
        if (statement[0]) strcat(statement, " ");
        strcat(statement, text);
        if (sqlite3_complete(statement)) {
            executeSql(db, shell, statement);
            statement[0] = '\0';
        }
    }
    if (statement[0])
        tsoOut("ERROR: incomplete SQL at end of input near line %d",
               lineNumber);
    return 0;
}

int main(void)
{
    sqlite3 *db = 0;
    char line[TSO_LINE];
    char statement[TSO_LINE];
    unsigned char raw[TSO_LINE];
    int length;
    int rc;
    ShellState shell;
    resetShell(&shell);
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
    executeSql(db, &shell,
               "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;");
    tsoOut("SQLite %s for MVS TSO", sqlite3_libversion());
    tsoOut("Use .help for commands");
    for (;;) {
        terminalOut(statement[0] ? "   ...> " : "sqlite> ");
        memset(line, 0, sizeof(line));
        memset(raw, 0, sizeof(raw));
        length = tsqtget((char *)raw, sizeof(raw));
        if (length < 0) break;
        if (length > 0 && raw[0] == TERM_PF3) break;
        if (length > 0 && raw[0] == TERM_CLEAR) {
            statement[0] = '\0';
            if (tsqtclr() != 0)
                tsoOut("ERROR: cannot clear terminal screen");
            continue;
        }
        if (length > 0 && raw[0] == TERM_ENTER) {
            length = termInput(line, sizeof(line), raw, length);
            if (length < 0) {
                tsoOut("ERROR: invalid terminal input (PF3 exits)");
                statement[0] = '\0';
                continue;
            }
        } else {
            if (length >= (int)sizeof(line)) length = sizeof(line) - 1;
            memcpy(line, raw, length);
            line[length] = '\0';
        }
        if (statement[0] == '\0' && trim(line)[0] == '.') {
            if (dotCommand(db, &shell, trim(line))) break;
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
            executeSql(db, &shell, statement);
            statement[0] = '\0';
        }
    }
    closeShellOutput();
    sqlite3_close(db);
    sqlite3_shutdown();
    tsoOut("SQLite TSO session ended");
    return 0;
}
