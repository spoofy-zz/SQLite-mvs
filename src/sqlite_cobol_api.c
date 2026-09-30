#include <stdio.h>
#include <string.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"
#include "sqlite_cobol.h"

typedef struct ApiOutput ApiOutput;
struct ApiOutput {
    char *text;
    int capacity;
    int length;
    int header;
    int rows;
    int overflow;
};

static void apiAppend(ApiOutput *out, const char *text)
{
    int count = (int)strlen(text);
    int available = out->capacity - out->length;
    if (available <= 1) {
        out->overflow = 1;
        return;
    }
    if (count >= available) {
        count = available - 1;
        out->overflow = 1;
    }
    memcpy(out->text + out->length, text, count);
    out->length += count;
    out->text[out->length] = '\0';
}

static int apiRow(void *context, int columns, char **values, char **names)
{
    ApiOutput *out = (ApiOutput *)context;
    int i;
    if (!out->header) {
        for (i = 0; i < columns; i++) {
            if (i) apiAppend(out, " | ");
            apiAppend(out, names[i]);
        }
        apiAppend(out, "\n");
        out->header = 1;
    }
    for (i = 0; i < columns; i++) {
        if (i) apiAppend(out, " | ");
        apiAppend(out, values[i] ? values[i] : "NULL");
    }
    apiAppend(out, "\n");
    out->rows++;
    return out->overflow ? 1 : 0;
}

void sqlite_cobol_entry(char *sql, short *sql_length,
                        char *output, short *output_capacity,
                        short *output_length, short *return_code)
{
    sqlite3 *db = 0;
    ApiOutput out;
    char statement[SQLITE_COBOL_MAX_SQL];
    char message[160];
    char *error = 0;
    int rc;
    int length;
    if (!return_code) return;
    *return_code = SQLITE_COBOL_RC_BAD_ARGUMENT;
    if (output_length) *output_length = 0;
    if (!sql || !sql_length || !output || !output_capacity ||
        !output_length || *sql_length < 0 || *output_capacity <= 1 ||
        *sql_length >= SQLITE_COBOL_MAX_SQL) return;
    length = *sql_length;
    memcpy(statement, sql, length);
    statement[length] = '\0';
    out.text = output;
    out.capacity = *output_capacity;
    out.length = 0;
    out.header = 0;
    out.rows = 0;
    out.overflow = 0;
    output[0] = '\0';

    rc = sqlite3_initialize();
    if (rc == SQLITE_OK)
        rc = sqlite3_open_v2("SQLDB", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    if (rc == SQLITE_OK)
        rc = sqlite3_exec(db, statement, apiRow, &out, &error);
    if (rc == SQLITE_OK && !out.rows) {
        snprintf(message, sizeof(message), "OK changes=%d\n",
                 sqlite3_changes(db));
        apiAppend(&out, message);
    }
    if (rc != SQLITE_OK) {
        apiAppend(&out, "ERROR: ");
        apiAppend(&out, error ? error :
                  (db ? sqlite3_errmsg(db) : "cannot open database"));
        apiAppend(&out, "\n");
    }
    if (error) sqlite3_free(error);
    if (db) sqlite3_close(db);
    sqlite3_shutdown();
    if (out.overflow) rc = SQLITE_TOOBIG;
    *output_length = (short)out.length;
    *return_code = (short)rc;
}
