#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

#define TSO_LINE 2048
#define TSO_OUT 512
#define TERM_ENTER 0x7d
#define TERM_CLEAR 0x6d
#define TERM_PF3   0xf3
#define MODE_COLUMN 0
#define MODE_LIST   1
#define COLUMN_WIDTH 18
#define MAX_COLUMNS 16

extern int tsqtget(char *buf, int max) asm("TSQTGET");
extern int tsqtput(char *buf, int len) asm("TSQTPUT");
extern int tsqtclr(void) asm("TSQTCLR");

typedef struct ShellState ShellState;
typedef struct RowOutput RowOutput;
struct ShellState {
    int headers;
    int mode;
    int echo;
    int timeout;
    int widths[MAX_COLUMNS];
    char nullValue[32];
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
    tsqtput(output, length);
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
            } else {
                int n;
                if (i) {
                    appendText(line, sizeof(line), &used, " | ");
                    appendText(rule, sizeof(rule), &ruleUsed, "-+-");
                }
                appendText(line, sizeof(line), &used, names[i]);
                for (n = 0; names[i][n]; n++)
                    appendText(rule, sizeof(rule), &ruleUsed, "-");
            }
        }
        tsoOut("%s", line);
        tsoOut("%s", rule);
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
        } else {
            if (i) appendText(line, sizeof(line), &used, " | ");
            appendText(line, sizeof(line), &used,
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
        tsoOut(".schema [table]     Show CREATE statements");
        tsoOut(".databases          List attached databases");
        tsoOut(".headers on|off     Show or hide column headers");
        tsoOut(".mode column|list   Select output format");
        tsoOut(".width N ...        Set column widths (1-80)");
        tsoOut(".nullvalue TEXT     Set NULL display text");
        tsoOut(".echo on|off        Echo SQL before execution");
        tsoOut(".timeout MS         Wait for locks (0 disables)");
        tsoOut(".show               Show shell settings");
        tsoOut(".clear              Clear screen and move cursor home");
        tsoOut(".version            Show SQLite version");
        tsoOut(".quit / .exit       Return to TSO READY");
        tsoOut("SQL statements must end with ;  PF3 exits");
    } else if (equalIgnoreCase(line, ".tables")) {
        executeSql(db, shell,
          "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    } else if (startsIgnoreCase(line, ".schema")) {
        char sql[TSO_OUT];
        char *name = trim(line + 7);
        if (*name)
            snprintf(sql, sizeof(sql),
                "SELECT sql FROM sqlite_master WHERE name='%s';", name);
        else
            strcpy(sql, "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY name;");
        executeSql(db, shell, sql);
    } else if (equalIgnoreCase(line, ".databases")) {
        executeSql(db, shell, "PRAGMA database_list;");
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
        }
        else tsoOut("Usage: .mode column|list");
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
               shell->mode == MODE_COLUMN ? "column" : "list");
        tsoOut("echo                %s", shell->echo ? "on" : "off");
        tsoOut("timeout             %d ms", shell->timeout);
        tsoOut("nullvalue           %s", shell->nullValue);
    } else if (equalIgnoreCase(line, ".clear")) {
        if (tsqtclr() != 0) tsoOut("ERROR: cannot clear terminal screen");
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
    unsigned char raw[TSO_LINE];
    int length;
    int rc;
    ShellState shell;
    int i;
    shell.headers = 1;
    shell.mode = MODE_COLUMN;
    shell.echo = 0;
    shell.timeout = 0;
    strcpy(shell.nullValue, "NULL");
    for (i = 0; i < MAX_COLUMNS; i++) shell.widths[i] = COLUMN_WIDTH;
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
        tsoOut(statement[0] ? "   ...> " : "sqlite> ");
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
    sqlite3_close(db);
    sqlite3_shutdown();
    tsoOut("SQLite TSO session ended");
    return 0;
}
