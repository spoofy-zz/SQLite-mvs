#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"
#include "sqlite_cobol_x.h"

static short getShort(unsigned char *request, int offset)
{
    short value;
    memcpy(&value, request + offset, sizeof(value));
    return value;
}

static void putShort(unsigned char *request, int offset, int value)
{
    short result = (short)value;
    memcpy(request + offset, &result, sizeof(result));
}

static void putText(unsigned char *target, int capacity, const char *text,
                    int *length)
{
    int count = text ? (int)strlen(text) : 0;
    if (count > capacity) count = capacity;
    memset(target, ' ', capacity);
    if (count) memcpy(target, text, count);
    if (length) *length = count;
}

static void setMessage(unsigned char *request, const char *text)
{
    int length;
    putText(request + SQLX_OFF_MESSAGE, 160, text, &length);
    putShort(request, SQLX_OFF_MESSAGE_LENGTH, length);
}

static int bindValues(sqlite3_stmt *statement, unsigned char *request)
{
    int count = getShort(request, SQLX_OFF_BIND_COUNT);
    int i;
    if (count < 0 || count > SQLX_MAX_BINDS) return SQLITE_RANGE;
    for (i = 0; i < count; i++) {
        unsigned char *bind = request + SQLX_OFF_BINDS + i * SQLX_BIND_SIZE;
        int length = getShort(bind, 2);
        int rc;
        if (length < 0 || length > SQLX_VALUE_SIZE) return SQLITE_RANGE;
        if (bind[0] == 'N') rc = sqlite3_bind_null(statement, i + 1);
        else if (bind[0] == 'I') {
            char value[SQLX_VALUE_SIZE + 1];
            memcpy(value, bind + 4, length);
            value[length] = '\0';
            rc = sqlite3_bind_int64(statement, i + 1,
                                    (sqlite3_int64)strtol(value, 0, 10));
        } else if (bind[0] == 'F') {
            char value[SQLX_VALUE_SIZE + 1];
            memcpy(value, bind + 4, length);
            value[length] = '\0';
            rc = sqlite3_bind_double(statement, i + 1, atof(value));
        } else if (bind[0] == 'B') {
            rc = sqlite3_bind_blob(statement, i + 1, bind + 4, length,
                                   SQLITE_TRANSIENT);
        } else {
            rc = sqlite3_bind_text(statement, i + 1, (char *)bind + 4,
                                   length, SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

static void putCell(unsigned char *cell, sqlite3_stmt *statement, int column)
{
    int type = sqlite3_column_type(statement, column);
    const void *value = 0;
    int length = 0;
    char number[SQLX_VALUE_SIZE + 1];
    memset(cell, ' ', SQLX_CELL_SIZE);
    if (type == SQLITE_NULL) {
        cell[0] = 'N';
    } else if (type == SQLITE_INTEGER) {
        cell[0] = 'I';
        snprintf(number, sizeof(number), "%lld",
                 sqlite3_column_int64(statement, column));
        value = number;
        length = (int)strlen(number);
    } else if (type == SQLITE_FLOAT) {
        cell[0] = 'F';
        snprintf(number, sizeof(number), "%.15g",
                 sqlite3_column_double(statement, column));
        value = number;
        length = (int)strlen(number);
    } else if (type == SQLITE_BLOB) {
        cell[0] = 'B';
        value = sqlite3_column_blob(statement, column);
        length = sqlite3_column_bytes(statement, column);
    } else {
        cell[0] = 'T';
        value = sqlite3_column_text(statement, column);
        length = sqlite3_column_bytes(statement, column);
    }
    if (length > SQLX_VALUE_SIZE) length = SQLX_VALUE_SIZE;
    putShort(cell, 2, length);
    if (length && value) memcpy(cell + 4, value, length);
}

void sqlite_cobol_x_entry(unsigned char *request)
{
    sqlite3 *db = 0;
    sqlite3_stmt *statement = 0;
    char sql[SQLX_MAX_SQL + 1];
    int length;
    int maxRows;
    int rows = 0;
    int columns = 0;
    int rc = SQLITE_COBOL_RC_BAD_ARGUMENT;
    int i;
    if (!request) return;
    memset(request + SQLX_OFF_ROW_COUNT, 0,
           SQLX_REQUEST_SIZE - SQLX_OFF_ROW_COUNT);
    length = getShort(request, SQLX_OFF_SQL_LENGTH);
    maxRows = getShort(request, SQLX_OFF_MAX_ROWS);
    if (length < 0 || length > SQLX_MAX_SQL || maxRows < 0 ||
        maxRows > SQLX_MAX_ROWS) goto done;
    memcpy(sql, request + SQLX_OFF_SQL, length);
    sql[length] = '\0';
    rc = sqlite3_initialize();
    if (rc == SQLITE_OK)
        rc = sqlite3_open_v2("SQLDB", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "mvs-rrds");
    if (rc == SQLITE_OK)
        rc = sqlite3_prepare_v2(db, sql, length, &statement, 0);
    if (rc == SQLITE_OK) rc = bindValues(statement, request);
    if (rc == SQLITE_OK) {
        columns = sqlite3_column_count(statement);
        if (columns > SQLX_MAX_COLUMNS) {
            rc = SQLITE_TOOBIG;
        } else {
            putShort(request, SQLX_OFF_COLUMN_COUNT, columns);
            for (i = 0; i < columns; i++)
                putText(request + SQLX_OFF_COLUMN_NAMES + i * 32, 32,
                        sqlite3_column_name(statement, i), 0);
        }
    }
    while (rc == SQLITE_OK && rows < maxRows) {
        rc = sqlite3_step(statement);
        if (rc != SQLITE_ROW) break;
        for (i = 0; i < columns; i++)
            putCell(request + SQLX_OFF_CELLS +
                    (rows * SQLX_MAX_COLUMNS + i) * SQLX_CELL_SIZE,
                    statement, i);
        rows++;
        if (rows < maxRows) rc = SQLITE_OK;
    }
    if (rc == SQLITE_DONE || (rc == SQLITE_ROW && rows == maxRows))
        rc = SQLITE_OK;
    putShort(request, SQLX_OFF_ROW_COUNT, rows);
    if (db) putShort(request, SQLX_OFF_CHANGES, sqlite3_changes(db));
done:
    if (rc == SQLITE_OK) setMessage(request, "OK");
    else setMessage(request, db ? sqlite3_errmsg(db) : "invalid request");
    if (statement) sqlite3_finalize(statement);
    if (db) sqlite3_close(db);
    sqlite3_shutdown();
    putShort(request, SQLX_OFF_RETURN_CODE, rc);
}
