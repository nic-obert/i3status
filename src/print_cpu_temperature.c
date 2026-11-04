#include <stdlib.h>
#include <limits.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 20

typedef struct temperature_s {
    double raw_value;
    char formatted_value[20];
} temperature_t;

#define ERROR_CODE 1

static int read_temperature(char *thermal_zone, temperature_t *temperature) {
    static char buf[16];
    long int temp;

    if (!slurp(thermal_zone, buf, sizeof(buf)))
        return ERROR_CODE;

    temp = strtol(buf, NULL, 10);
    temperature->raw_value = temp / 1000;

    if (temp == LONG_MIN || temp == LONG_MAX || temp <= 0)
        strcpy(temperature->formatted_value, "?");
    else
        sprintf(temperature->formatted_value, "%ld", (temp / 1000));

    return 0;
}

/*
 * Reads the CPU temperature from /sys/class/thermal/thermal_zone%d/temp (or
 * the user provided path) and returns the temperature in degree celsius.
 *
 */
void print_cpu_temperature_info(cpu_temperature_ctx_t *ctx) {
    char *outwalk = ctx->buf;
#ifdef THERMAL_ZONE
    const char *selected_format = ctx->format;
    bool colorful_output = false;
    char *thermal_zone;
    temperature_t temperature;
    temperature.raw_value = 0;
    sprintf(temperature.formatted_value, "%.2f", 0.0);

    if (ctx->path == NULL)
        asprintf(&thermal_zone, THERMAL_ZONE, ctx->zone);
    else {
        static glob_t globbuf;
        if (glob(ctx->path, GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf) != 0)
            die("glob() failed\n");
        if (globbuf.gl_pathc == 0) {
            /* No glob matches, the specified path does not contain a wildcard. */
            asprintf(&thermal_zone, ctx->path, ctx->zone);
        } else {
            /* glob matched, we take the first match and ignore the others */
            asprintf(&thermal_zone, "%s", globbuf.gl_pathv[0]);
        }
        globfree(&globbuf);
    }

    INSTANCE(thermal_zone);

    if (read_temperature(thermal_zone, &temperature) != 0)
        goto error;

    if (temperature.raw_value >= ctx->max_threshold) {
        START_COLOR("color_bad");
        colorful_output = true;
        if (ctx->format_above_threshold != NULL)
            selected_format = ctx->format_above_threshold;
    }

    char string_degrees[STRING_SIZE];
    snprintf(string_degrees, STRING_SIZE, "%s", temperature.formatted_value);
    placeholder_t placeholders[] = {
        {.name = "%degrees", .value = string_degrees}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(selected_format, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);

    if (colorful_output) {
        END_COLOR;
        colorful_output = false;
    }

    free(thermal_zone);

    OUTPUT_FULL_TEXT(ctx->buf);
    return;
error:
    free(thermal_zone);
#endif

    OUTPUT_FULL_TEXT("can't read temp");
    (void)fputs("i3status: Cannot read temperature. Verify that you have a thermal zone in /sys/class/thermal or disable the cpu_temperature module in your i3status config.\n", stderr);
}
