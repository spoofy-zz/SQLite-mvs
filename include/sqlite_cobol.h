#ifndef SQLITE_COBOL_H
#define SQLITE_COBOL_H

#define SQLITE_COBOL_MAX_SQL 4096
#define SQLITE_COBOL_RC_BAD_ARGUMENT 100

void sqlite_cobol_entry(char *sql, short *sql_length,
                        char *output, short *output_capacity,
                        short *output_length, short *return_code);

#endif
