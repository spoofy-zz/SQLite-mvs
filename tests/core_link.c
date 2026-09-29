#include <stdio.h>

#include "sqlite3_mvs_names.h"
#include "sqlite3.h"

int main(void)
{
    printf("SQLite %s (%d)\n", sqlite3_libversion(),
           sqlite3_libversion_number());
    return 0;
}

