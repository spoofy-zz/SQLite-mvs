/*
 * MVS 3.8j SQLite OS bootstrap.
 *
 * The compiler/assembler viability gate is now green.  This file deliberately
 * registers no VFS yet: the next increment will add the VSAM RRDS-backed
 * sqlite3_vfs and sqlite3_io_methods tables here.
 */
#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

int sqlite3_os_init(void)
{
    return SQLITE_OK;
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}

