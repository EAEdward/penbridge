#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>

#include "penbridge.h"

/* Line buffered on stdout, which service.sh redirects to the module log.
 * Every line is prefixed so a shared log stays readable. */
void pb_log(const char *fmt, ...)
{
    va_list ap;
    fputs("[penbridge] ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}
