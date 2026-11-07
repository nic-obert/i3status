#include <config.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <errno.h>

#include "i3status.h"

struct cpu_usage {
    int user;
    int nice;
    int system;
    int idle;
    int total;
};

static int cpu_count = 0;
static struct cpu_usage prev_all = {0, 0, 0, 0, 0};
static struct cpu_usage *prev_cpus = NULL;
static struct cpu_usage *curr_cpus = NULL;

/*
 * Reads the CPU utilization from /proc/stat and returns the usage as a
 * percentage.
 *
 */
void print_cpu_usage(cpu_usage_ctx_t *ctx, output_ctx_t *out_ctx) {
    const char *selected_format = ctx->format;
    const char *walk;
    char *outwalk = out_ctx->buf;
    struct cpu_usage curr_all = {0, 0, 0, 0, 0};
    int diff_idle, diff_total;
    int diff_usage;

    // Detecting if CPU count has changed
    int curr_cpu_count = get_nprocs_conf();
    if (curr_cpu_count != cpu_count) {
        cpu_count = curr_cpu_count;
        free(prev_cpus);
        prev_cpus = (struct cpu_usage *)calloc(cpu_count, sizeof(struct cpu_usage));
        free(curr_cpus);
        curr_cpus = (struct cpu_usage *)calloc(cpu_count, sizeof(struct cpu_usage));
    }

    memcpy(curr_cpus, prev_cpus, cpu_count * sizeof(struct cpu_usage));
    FILE *f = fopen(ctx->path, "r");
    if (f == NULL) {
        fprintf(stderr, "i3status: open %s: %s\n", ctx->path, strerror(errno));
        goto error;
    }
    curr_cpu_count = get_nprocs();
    char line[4096];

    /* Discard first line (cpu ), start at second line (cpu0) */
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        goto error; /* unexpected EOF or read error */
    }

    for (int idx = 0; idx < curr_cpu_count; ++idx) {
        if (fgets(line, sizeof(line), f) == NULL) {
            fclose(f);
            goto error; /* unexpected EOF or read error */
        }
        int cpu_idx, user, nice, system, idle;
        if (sscanf(line, "cpu%d %d %d %d %d", &cpu_idx, &user, &nice, &system, &idle) != 5) {
            fclose(f);
            goto error;
        }
        if (cpu_idx < 0 || cpu_idx >= cpu_count) {
            fclose(f);
            goto error;
        }
        curr_cpus[cpu_idx].user = user;
        curr_cpus[cpu_idx].nice = nice;
        curr_cpus[cpu_idx].system = system;
        curr_cpus[cpu_idx].idle = idle;
        curr_cpus[cpu_idx].total = user + nice + system + idle;
    }
    fclose(f);
    for (int cpu_idx = 0; cpu_idx < cpu_count; cpu_idx++) {
        curr_all.user += curr_cpus[cpu_idx].user;
        curr_all.nice += curr_cpus[cpu_idx].nice;
        curr_all.system += curr_cpus[cpu_idx].system;
        curr_all.idle += curr_cpus[cpu_idx].idle;
        curr_all.total += curr_cpus[cpu_idx].total;
    }

    diff_idle = curr_all.idle - prev_all.idle;
    diff_total = curr_all.total - prev_all.total;
    diff_usage = (diff_total ? (1000 * (diff_total - diff_idle) / diff_total + 5) / 10 : 0);
    prev_all = curr_all;

    if (diff_usage >= ctx->max_threshold) {
        START_COLOR(bad);
        if (ctx->format_above_threshold != NULL)
            selected_format = ctx->format_above_threshold;
    } else if (diff_usage >= ctx->degraded_threshold) {
        START_COLOR(degraded);
        if (ctx->format_above_degraded_threshold != NULL)
            selected_format = ctx->format_above_degraded_threshold;
    }

    for (walk = selected_format; *walk != '\0'; walk++) {
        if (*walk != '%') {
            *(outwalk++) = *walk;

        } else if (BEGINS_WITH(walk + 1, "usage")) {
            outwalk += sprintf(outwalk, "%02d%s", diff_usage, "%");
            walk += strlen("usage");
        }
        else if (BEGINS_WITH(walk + 1, "cpu")) {
            int number = -1;
            int length = strlen("cpu");
            sscanf(walk + 1, "cpu%d%n", &number, &length);
            if (number == -1) {
                fprintf(stderr, "i3status: provided CPU number cannot be parsed\n");
            } else if (number >= cpu_count) {
                fprintf(stderr, "i3status: provided CPU number '%d' above detected number of CPU %d\n", number, cpu_count);
            } else {
                int cpu_diff_idle = curr_cpus[number].idle - prev_cpus[number].idle;
                int cpu_diff_total = curr_cpus[number].total - prev_cpus[number].total;
                int cpu_diff_usage = (cpu_diff_total ? (1000 * (cpu_diff_total - cpu_diff_idle) / cpu_diff_total + 5) / 10 : 0);
                outwalk += sprintf(outwalk, "%02d%s", cpu_diff_usage, "%");
            }
            walk += length;
        }
        else {
            *(outwalk++) = '%';
        }
    }

    struct cpu_usage *temp_cpus = prev_cpus;
    prev_cpus = curr_cpus;
    curr_cpus = temp_cpus;

    OUTPUT_FULL_TEXT(out_ctx->buf);
    return;
error:
    OUTPUT_FULL_TEXT("cant read cpu usage");
    (void)fputs("i3status: Cannot read CPU usage\n", stderr);
}
