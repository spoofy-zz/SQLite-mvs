/* SQLite VFS for MVS 3.8j backed by a fixed-record VSAM RRDS. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"
#include <clibvsam.h>

#define MVS_PAGE_SIZE 4096
#define MVS_DDNAME "SQLDB"

typedef struct MvsFile MvsFile;
struct MvsFile {
    sqlite3_file base;
    VSFILE *vs;
    sqlite3_int64 size;
    int lock;
};

static int mvsClose(sqlite3_file *file);
static int mvsRead(sqlite3_file *file, void *buf, int amount, sqlite3_int64 offset);
static int mvsWrite(sqlite3_file *file, const void *buf, int amount, sqlite3_int64 offset);
static int mvsTruncate(sqlite3_file *file, sqlite3_int64 size);
static int mvsSync(sqlite3_file *file, int flags);
static int mvsFileSize(sqlite3_file *file, sqlite3_int64 *size);
static int mvsLock(sqlite3_file *file, int lock);
static int mvsUnlock(sqlite3_file *file, int lock);
static int mvsCheckReservedLock(sqlite3_file *file, int *result);
static int mvsFileControl(sqlite3_file *file, int op, void *arg);
static int mvsSectorSize(sqlite3_file *file);
static int mvsDeviceCharacteristics(sqlite3_file *file);

static const sqlite3_io_methods mvsIoMethods = {
    1, mvsClose, mvsRead, mvsWrite, mvsTruncate, mvsSync, mvsFileSize,
    mvsLock, mvsUnlock, mvsCheckReservedLock, mvsFileControl, mvsSectorSize,
    mvsDeviceCharacteristics, 0, 0, 0, 0, 0, 0
};

static unsigned mvsGet32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static int mvsReopen(MvsFile *file)
{
    if (__vsclos(file->vs) != 0) return SQLITE_IOERR_CLOSE;
    file->vs = 0;
    if (__vsopen(MVS_DDNAME, VSTYPE_RRDS, VSACCESS_DIR, VSMODE_UPD,
                 &file->vs) != 0 || file->vs == 0)
        return SQLITE_CANTOPEN;
    return SQLITE_OK;
}

static int mvsReadPage(MvsFile *file, unsigned rrn, unsigned char *page)
{
    int key = (int)rrn;
    int rc;
    memset(page, 0, MVS_PAGE_SIZE);
    __vsclr(file->vs);
    rc = __vsread(file->vs, page, MVS_PAGE_SIZE, &key, sizeof(key));
    if (rc == MVS_PAGE_SIZE) return 1;
    __vsclr(file->vs);
    return 0;
}

static int mvsWritePage(MvsFile *file, unsigned rrn, const unsigned char *page)
{
    unsigned char old[MVS_PAGE_SIZE];
    int key = (int)rrn;
    if (mvsReadPage(file, rrn, old)) {
        int rc = __vsupdt(file->vs, (void *)page, MVS_PAGE_SIZE);
        if (rc == 0) return SQLITE_OK;
    } else {
        int rc;
        __vsclr(file->vs);
        rc = __vswrit(file->vs, (void *)page, MVS_PAGE_SIZE,
                      &key, sizeof(key));
        if (rc == 0) {
            /* A newly defined RRDS remains in initial-load state until it
               is closed.  In that state MVS rejects every GET with x'74'.
               End initial load after an insert so SQLite may immediately
               read the pages it just created. */
            return mvsReopen(file);
        }
    }
    __vsclr(file->vs);
    return SQLITE_IOERR_WRITE;
}

static int mvsClose(sqlite3_file *base)
{
    MvsFile *file = (MvsFile *)base;
    int rc = SQLITE_OK;
    if (file->vs && __vsclos(file->vs) != 0) rc = SQLITE_IOERR_CLOSE;
    file->vs = 0;
    file->base.pMethods = 0;
    return rc;
}

static int mvsRead(sqlite3_file *base, void *out, int amount, sqlite3_int64 offset)
{
    MvsFile *file = (MvsFile *)base;
    unsigned char page[MVS_PAGE_SIZE];
    unsigned char *dest = (unsigned char *)out;
    int remaining = amount;
    int shortRead = 0;
    while (remaining > 0) {
        unsigned rrn = (unsigned)(offset / MVS_PAGE_SIZE) + 1;
        int within = (int)(offset % MVS_PAGE_SIZE);
        int chunk = MVS_PAGE_SIZE - within;
        if (chunk > remaining) chunk = remaining;
        if (!mvsReadPage(file, rrn, page)) shortRead = 1;
        memcpy(dest, page + within, (size_t)chunk);
        dest += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    return shortRead ? SQLITE_IOERR_SHORT_READ : SQLITE_OK;
}

static int mvsWrite(sqlite3_file *base, const void *in, int amount,
                    sqlite3_int64 offset)
{
    MvsFile *file = (MvsFile *)base;
    const unsigned char *source = (const unsigned char *)in;
    unsigned char page[MVS_PAGE_SIZE];
    sqlite3_int64 end = offset + amount;
    int remaining = amount;
    while (remaining > 0) {
        unsigned rrn = (unsigned)(offset / MVS_PAGE_SIZE) + 1;
        int within = (int)(offset % MVS_PAGE_SIZE);
        int chunk = MVS_PAGE_SIZE - within;
        if (chunk > remaining) chunk = remaining;
        if (within != 0 || chunk != MVS_PAGE_SIZE)
            mvsReadPage(file, rrn, page);
        else
            memset(page, 0, sizeof(page));
        memcpy(page + within, source, (size_t)chunk);
        if (mvsWritePage(file, rrn, page) != SQLITE_OK)
            return SQLITE_IOERR_WRITE;
        source += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    if (end > file->size) file->size = end;
    return SQLITE_OK;
}

static int mvsTruncate(sqlite3_file *base, sqlite3_int64 size)
{
    /* Physical ERASE of trailing RRNs comes with the transaction layer. */
    ((MvsFile *)base)->size = size;
    return SQLITE_OK;
}

static int mvsSync(sqlite3_file *base, int flags)
{ (void)base; (void)flags; return SQLITE_OK; }

static int mvsFileSize(sqlite3_file *base, sqlite3_int64 *size)
{ *size = ((MvsFile *)base)->size; return SQLITE_OK; }

static int mvsLock(sqlite3_file *base, int lock)
{ MvsFile *f = (MvsFile *)base; if (lock > f->lock) f->lock = lock; return SQLITE_OK; }

static int mvsUnlock(sqlite3_file *base, int lock)
{ ((MvsFile *)base)->lock = lock; return SQLITE_OK; }

static int mvsCheckReservedLock(sqlite3_file *base, int *result)
{ *result = ((MvsFile *)base)->lock >= SQLITE_LOCK_RESERVED; return SQLITE_OK; }

static int mvsFileControl(sqlite3_file *base, int op, void *arg)
{
    if (op == SQLITE_FCNTL_LOCKSTATE) {
        *(int *)arg = ((MvsFile *)base)->lock;
        return SQLITE_OK;
    }
    return SQLITE_NOTFOUND;
}

static int mvsSectorSize(sqlite3_file *base)
{ (void)base; return MVS_PAGE_SIZE; }

static int mvsDeviceCharacteristics(sqlite3_file *base)
{ (void)base; return 0; }

static int mvsOpen(sqlite3_vfs *vfs, const char *name, sqlite3_file *base,
                   int flags, int *outFlags)
{
    MvsFile *file = (MvsFile *)base;
    unsigned char first[MVS_PAGE_SIZE];
    int rc;
    (void)vfs; (void)name;
    memset(file, 0, sizeof(*file));
    if ((flags & SQLITE_OPEN_MAIN_DB) == 0) return SQLITE_CANTOPEN;
    rc = __vsopen(MVS_DDNAME, VSTYPE_RRDS, VSACCESS_DIR, VSMODE_UPD,
                  &file->vs);
    if (rc != 0 || file->vs == 0) return SQLITE_CANTOPEN;
    file->base.pMethods = &mvsIoMethods;
    if (mvsReadPage(file, 1, first) &&
        memcmp(first, "SQLite format 3\000", 16) == 0)
        file->size = (sqlite3_int64)mvsGet32(first + 28) * MVS_PAGE_SIZE;
    if (outFlags) *outFlags = flags;
    return SQLITE_OK;
}

static int mvsDelete(sqlite3_vfs *vfs, const char *name, int syncDir)
{ (void)vfs; (void)name; (void)syncDir; return SQLITE_IOERR_DELETE; }

static int mvsAccess(sqlite3_vfs *vfs, const char *name, int flags, int *result)
{
    (void)vfs; (void)flags;
    *result = name != 0 && strcmp(name, MVS_DDNAME) == 0;
    return SQLITE_OK;
}

static int mvsFullPathname(sqlite3_vfs *vfs, const char *name, int outSize,
                           char *out)
{
    size_t length;
    (void)vfs;
    if (!name) name = MVS_DDNAME;
    length = strlen(name);
    if ((int)length >= outSize) return SQLITE_CANTOPEN;
    memcpy(out, name, length + 1);
    return SQLITE_OK;
}

static int mvsRandomness(sqlite3_vfs *vfs, int amount, char *out)
{
    static unsigned state;
    int i;
    (void)vfs;
    if (state == 0) state = (unsigned)time(0) ^ (unsigned)(size_t)out;
    for (i = 0; i < amount; i++) {
        state = state * 1103515245U + 12345U;
        out[i] = (char)(state >> 16);
    }
    return amount;
}

static int mvsSleep(sqlite3_vfs *vfs, int microseconds)
{
    clock_t begin = clock();
    clock_t ticks = (clock_t)(((long)microseconds * CLOCKS_PER_SEC) / 1000000L);
    (void)vfs;
    while (clock() - begin < ticks) { }
    return microseconds;
}

static int mvsCurrentTime(sqlite3_vfs *vfs, double *julian)
{ (void)vfs; *julian = 2440587.5 + ((double)time(0) / 86400.0); return SQLITE_OK; }

static sqlite3_vfs mvsVfs = {
    1, sizeof(MvsFile), 44, 0, "mvs-rrds", 0,
    mvsOpen, mvsDelete, mvsAccess, mvsFullPathname,
    0, 0, 0, 0, mvsRandomness, mvsSleep, mvsCurrentTime, 0,
    0, 0, 0, 0
};

int sqlite3_os_init(void)
{ return sqlite3_vfs_register(&mvsVfs, 1); }

int sqlite3_os_end(void)
{ return sqlite3_vfs_unregister(&mvsVfs); }
