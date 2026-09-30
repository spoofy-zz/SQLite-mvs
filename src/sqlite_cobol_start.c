#ifdef __MVS__
#include <clibcrt.h>
#include "sqlite_cobol.h"

int api_start(char *parm, char *name, int job, unsigned long *args)
    asm("@@START");
int api_start(char *parm, char *name, int job, unsigned long *args)
{
    void *a[6];
    int i;
    (void)parm;
    (void)name;
    (void)job;
    for (i = 0; i < 6; i++) a[i] = (void *)(args[i] & 0x00ffffffUL);
    sqlite_cobol_entry(a[0], a[1], a[2], a[3], a[4], a[5]);
    __exit(*(short *)a[5]);
    return 16;
}
#endif
