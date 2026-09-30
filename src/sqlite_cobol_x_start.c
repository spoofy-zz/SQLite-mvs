#ifdef __MVS__
#include <clibcrt.h>
#include "sqlite_cobol_x.h"

int api_x_start(char *parm, char *name, int job, unsigned long *args)
    asm("@@START");
int api_x_start(char *parm, char *name, int job, unsigned long *args)
{
    unsigned char *request;
    (void)parm;
    (void)name;
    (void)job;
    request = (unsigned char *)(args[0] & 0x00ffffffUL);
    sqlite_cobol_x_entry(request);
    __exit(request ? *(short *)(request + 2612) : 100);
    return 16;
}
#endif
