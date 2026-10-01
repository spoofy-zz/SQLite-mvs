/* MVS/cc370 policy layer. Keep upstream sqlite3.c and sqlite3.h unmodified. */
#ifndef SQLITE3_MVS_COMPAT_H
#define SQLITE3_MVS_COMPAT_H

#ifndef SQLITE_OS_OTHER
#define SQLITE_OS_OTHER 1
#endif
#ifndef SQLITE_THREADSAFE
#define SQLITE_THREADSAFE 0
#endif
#ifndef SQLITE_OMIT_LOAD_EXTENSION
#define SQLITE_OMIT_LOAD_EXTENSION 1
#endif
#ifndef SQLITE_OMIT_WAL
#define SQLITE_OMIT_WAL 1
#endif
#ifndef SQLITE_TEMP_STORE
#define SQLITE_TEMP_STORE 3
#endif
#ifndef SQLITE_DEFAULT_PAGE_SIZE
#define SQLITE_DEFAULT_PAGE_SIZE 4096
#endif
#ifndef SQLITE_MAX_DEFAULT_PAGE_SIZE
#define SQLITE_MAX_DEFAULT_PAGE_SIZE 4096
#endif
#ifndef SQLITE_MAX_MMAP_SIZE
#define SQLITE_MAX_MMAP_SIZE 0
#endif
#ifndef SQLITE_OMIT_AUTOINIT
#define SQLITE_OMIT_AUTOINIT 1
#endif

#endif
