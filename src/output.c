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
    if (!cfg_getbool(cfg_general, "colors")) {
        colorbuf[0] = '\0';
    }
    return colorbuf;
}

void print_separator(const char *separator) {
    if (output_format == O_I3BAR || strlen(separator) == 0)
        return;
    if (output_format == O_NONE)
        printf("%s", separator);
}

/*
 * The term-output hides the cursor. We call this on exit to reset that.
 */
void reset_cursor(void) {
    printf("\033[?25h");
}

/*
 * Escapes ampersand, less-than, greater-than, single-quote, and double-quote
 * characters with the corresponding Pango markup strings if markup is enabled.
 * See the glib implementation:
 * https://git.gnome.org/browse/glib/tree/glib/gmarkup.c?id=03db1f455b4265654e237d2ad55464b4113cba8a#n2142
 *
 */
void maybe_escape_markup(char *text, char *buffer, size_t size) {
    size--; /* Leave a byte for NUL termination. */

    if (markup_format == M_NONE) {
        snprintf(buffer, size, "%s", text);
        return;
    }

    for (size_t i = 0; *text != '\0'; text++) {
        if (i >= size) {
            return;
        }
        switch (*text) {
            case '&':
                i += snprintf(&buffer[i], size - i, "%s", "&amp;");
                break;
            case '<':
                i += snprintf(&buffer[i], size - i, "%s", "&lt;");
                break;
            case '>':
                i += snprintf(&buffer[i], size - i, "%s", "&gt;");
                break;
            case '\'':
                i += snprintf(&buffer[i], size - i, "%s", "&apos;");
                break;
            case '"':
                i += snprintf(&buffer[i], size - i, "%s", "&quot;");
                break;
            default:
                if ((0x1 <= *text && *text <= 0x8) ||
                    (0xb <= *text && *text <= 0xc) ||
                    (0xe <= *text && *text <= 0x1f)) {
                    i += snprintf(&buffer[i], size - i, "&#x%x;", *text);
                } else {
                    buffer[i] = *text;
                    i++;
                }
                break;
        }
    }
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
