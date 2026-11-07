#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>

#include "i3status.h"


char *color(const char *colorstr) {
    static char colorbuf[32];
    if (!general_cfg->colors) {
        colorbuf[0] = '\0';
    }
    return colorbuf;
}

/*
 * The term-output hides the cursor. We call this on exit to reset that.
 */
void reset_cursor(void) {
    printf("\033[?25h");
}

/*
 * remove leading spaces
 */
char *ltrim(const char *s) {
    while (isspace(*s))
        ++s;
    return sstrdup(s);
}

/*
 * remove trailing spaces
 */
char *rtrim(const char *s) {
    char *r = sstrdup(s);
    if (r != NULL) {
        char *fr = r + strlen(s) - 1;
        while ((isspace(*fr) || *fr == 0) && fr >= r)
            --fr;
        *++fr = 0;
    }
    return r;
}

/*
 * remove leading & trailing spaces
 */
char *trim(const char *s) {
    char *r = rtrim(s);
    char *f = ltrim(r);
    free(r);
    return f;
}
