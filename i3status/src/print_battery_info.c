#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 128

#include <glob.h>
#include <sys/types.h>

typedef enum {
    CS_UNKNOWN,
    CS_DISCHARGING,
    CS_CHARGING,
    CS_FULL,
    CS_IDLE,
} charging_status_t;

/* A description of the state of one or more batteries. */
struct battery_info {
    /* measured properties */
    int full_design;  /* in uAh */
    int full_last;    /* in uAh */
    int remaining;    /* in uAh */
    int present_rate; /* in uA, always non-negative */

    /* derived properties */
    int seconds_remaining;
    float percentage_remaining;
    charging_status_t status;
};

/*
 * Add batt_info data to acc.
 */
static void add_battery_info(struct battery_info *acc, const struct battery_info *batt_info) {
    if (acc->remaining < 0) {
        /* initialize accumulator so we can add to it */
        acc->full_design = 0;
        acc->full_last = 0;
        acc->remaining = 0;
        acc->present_rate = 0;
    }

    acc->full_design += batt_info->full_design;
    acc->full_last += batt_info->full_last;
    acc->remaining += batt_info->remaining;

    /* make present_rate negative for discharging and positive for charging */
    int present_rate = (acc->status == CS_DISCHARGING ? -1 : 1) * acc->present_rate;
    present_rate += (batt_info->status == CS_DISCHARGING ? -1 : 1) * batt_info->present_rate;

    /* merge status */
    switch (acc->status) {
        case CS_UNKNOWN:
            acc->status = batt_info->status;
            break;

        case CS_DISCHARGING:
            if (present_rate > 0)
                acc->status = CS_CHARGING;
            /* else if batt_info is DISCHARGING: no conflict
             * else if batt_info is CHARGING: present_rate should indicate that
             * else if batt_info is FULL: but something else is discharging */
            break;

        case CS_CHARGING:
            if (present_rate < 0)
                acc->status = CS_DISCHARGING;
            /* else if batt_info is DISCHARGING: present_rate should indicate that
             * else if batt_info is CHARGING: no conflict
             * else if batt_info is FULL: but something else is charging */
            break;

        case CS_FULL:
            if (batt_info->status != CS_UNKNOWN)
                acc->status = batt_info->status;
            /* else: retain FULL, since it is more specific than UNKNOWN */
            break;

        case CS_IDLE:
            if (batt_info->status != CS_UNKNOWN && batt_info->status != CS_FULL)
                acc->status = batt_info->status;
            /* else: retain IDLE, since it is more specific than UNKNOWN and is
             * implied by CS_FULL though correct for all batteries */
            break;
    }

    acc->present_rate = abs(present_rate);
}

static bool slurp_battery_info(battery_info_ctx_t *ctx, struct battery_info *batt_info, yajl_gen json_gen, char *buffer, int number, const char *path, const char *format_down) {
    char *outwalk = buffer;

    char buf[1024];
    const char *walk, *last;
    bool watt_as_unit = false;
    int voltage = -1;
    char batpath[512];
    sprintf(batpath, path, number);
    INSTANCE(batpath);

    if (!slurp(batpath, buf, sizeof(buf))) {
        OUTPUT_FULL_TEXT(format_down);
        return false;
    }

    for (walk = buf, last = buf; (walk - buf) < 1024; walk++) {
        // `*walk` (slice of `buf`) is only initialised until `null` written by `slurp()`
        if (*walk == '\0') {
            break;
        }

        if (*walk == '\n') {
            last = walk + 1;
            continue;
        }

        if (*walk != '=')
            continue;

        if (BEGINS_WITH(last, "POWER_SUPPLY_ENERGY_NOW=")) {
            watt_as_unit = true;
            batt_info->remaining = atoi(walk + 1);
            batt_info->percentage_remaining = -1;
        } else if (BEGINS_WITH(last, "POWER_SUPPLY_CHARGE_NOW=")) {
            watt_as_unit = false;
            batt_info->remaining = atoi(walk + 1);
            batt_info->percentage_remaining = -1;
        } else if (BEGINS_WITH(last, "POWER_SUPPLY_CAPACITY=") && batt_info->remaining == -1) {
            batt_info->percentage_remaining = atoi(walk + 1);
        } else if (BEGINS_WITH(last, "POWER_SUPPLY_CURRENT_NOW="))
            batt_info->present_rate = abs(atoi(walk + 1));
        else if (BEGINS_WITH(last, "POWER_SUPPLY_VOLTAGE_NOW="))
            voltage = abs(atoi(walk + 1));
        else if (BEGINS_WITH(last, "POWER_SUPPLY_TIME_TO_EMPTY_NOW="))
            batt_info->seconds_remaining = abs(atoi(walk + 1)) * 60;
        /* on some systems POWER_SUPPLY_POWER_NOW does not exist, but actually
         * it is the same as POWER_SUPPLY_CURRENT_NOW but with μWh as
         * unit instead of μAh. We will calculate it as we need it
         * later. */
        else if (BEGINS_WITH(last, "POWER_SUPPLY_POWER_NOW="))
            batt_info->present_rate = abs(atoi(walk + 1));
        else if (BEGINS_WITH(last, "POWER_SUPPLY_STATUS=Charging"))
            batt_info->status = CS_CHARGING;
        else if (BEGINS_WITH(last, "POWER_SUPPLY_STATUS=Full"))
            batt_info->status = CS_FULL;
        else if (BEGINS_WITH(last, "POWER_SUPPLY_STATUS=Discharging"))
            batt_info->status = CS_DISCHARGING;
        else if (BEGINS_WITH(last, "POWER_SUPPLY_STATUS=Not charging"))
            batt_info->status = CS_IDLE;
        else if (BEGINS_WITH(last, "POWER_SUPPLY_STATUS="))
            batt_info->status = CS_UNKNOWN;
        else if (BEGINS_WITH(last, "POWER_SUPPLY_CHARGE_FULL_DESIGN=") ||
                 BEGINS_WITH(last, "POWER_SUPPLY_ENERGY_FULL_DESIGN="))
            batt_info->full_design = atoi(walk + 1);
        else if (BEGINS_WITH(last, "POWER_SUPPLY_ENERGY_FULL=") ||
                 BEGINS_WITH(last, "POWER_SUPPLY_CHARGE_FULL="))
            batt_info->full_last = atoi(walk + 1);
    }

    /* the difference between POWER_SUPPLY_ENERGY_NOW and
     * POWER_SUPPLY_CHARGE_NOW is the unit of measurement. The energy is
     * given in mWh, the charge in mAh. So calculate every value given in
     * ampere to watt */
    if (!watt_as_unit && voltage >= 0) {
        if (batt_info->present_rate > 0) {
            batt_info->present_rate = (((float)voltage / 1000.0) * ((float)batt_info->present_rate / 1000.0));
        }
        if (batt_info->remaining > 0) {
            batt_info->remaining = (((float)voltage / 1000.0) * ((float)batt_info->remaining / 1000.0));
        }
        if (batt_info->full_design > 0) {
            batt_info->full_design = (((float)voltage / 1000.0) * ((float)batt_info->full_design / 1000.0));
        }
        if (batt_info->full_last > 0) {
            batt_info->full_last = (((float)voltage / 1000.0) * ((float)batt_info->full_last / 1000.0));
        }
    }

    return true;
}

/*
 * Populate batt_info with aggregate information about all batteries.
 * Returns false on error, and an error message will have been written.
 */
static bool slurp_all_batteries(battery_info_ctx_t *ctx, struct battery_info *batt_info, yajl_gen json_gen, char *buffer, const char *path, const char *format_down) {
    char *outwalk = buffer;
    bool is_found = false;

    char *placeholder;
    char *globpath = sstrdup(path);
    if ((placeholder = strstr(path, "%d")) != NULL) {
        char *globplaceholder = globpath + (placeholder - path);
        *globplaceholder = '*';
        strcpy(globplaceholder + 1, placeholder + 2);
    }

    if (!strcmp(globpath, path)) {
        OUTPUT_FULL_TEXT("no '%d' in battery path");
        return false;
    }

    glob_t globbuf;
    if (glob(globpath, 0, NULL, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            /* Probe to see if there is such a battery. */
            struct battery_info batt_buf = {
                .full_design = 0,
                .full_last = 0,
                .remaining = 0,
                .present_rate = 0,
                .status = CS_UNKNOWN,
            };
            if (!slurp_battery_info(ctx, &batt_buf, json_gen, buffer, i, globbuf.gl_pathv[i], format_down)) {
                globfree(&globbuf);
                free(globpath);
                return false;
            }

            is_found = true;
            add_battery_info(batt_info, &batt_buf);
        }
        globfree(&globbuf);
    }
    free(globpath);

    if (!is_found) {
        OUTPUT_FULL_TEXT(format_down);
        return false;
    }

    batt_info->present_rate = abs(batt_info->present_rate);

    return true;
}

void print_battery_info(battery_info_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    struct battery_info batt_info = {
        .full_design = -1,
        .full_last = -1,
        .remaining = -1,
        .present_rate = -1,
        .seconds_remaining = -1,
        .percentage_remaining = -1,
        .status = CS_UNKNOWN,
    };
    bool colorful_output = false;

    if (ctx->number < 0) {
        if (!slurp_all_batteries(ctx, &batt_info, ctx->json_gen, ctx->buf, ctx->path, ctx->format_down))
            return;
    } else {
        if (!slurp_battery_info(ctx, &batt_info, ctx->json_gen, ctx->buf, ctx->number, ctx->path, ctx->format_down))
            return;
    }

    // *Choose* a measure of the 'full' battery. It is whichever is better of
    // the battery's (hardware-given) design capacity (batt_info.full_design)
    // and the battery's last known good charge (batt_info.full_last).
    // We prefer the design capacity, but use the last capacity if we don't have it,
    // or if we are asked to (last_full_capacity == true); but similarly we use
    // the design capacity if we don't have the last capacity.
    // If we don't have either then both full_design and full_last <= 0,
    // which implies full <= 0, which bails out on the following line.
    int full = batt_info.full_design;
    if (full <= 0 || (ctx->last_full_capacity && batt_info.full_last > 0)) {
        full = batt_info.full_last;
    }
    if (full <= 0 && batt_info.remaining < 0 && batt_info.percentage_remaining < 0) {
        /* We have no physical measurements and no estimates. Nothing
         * much we can report, then. */
        OUTPUT_FULL_TEXT(ctx->format_down);
        return;
    }

    if (batt_info.percentage_remaining < 0) {
        batt_info.percentage_remaining = (((float)batt_info.remaining / (float)full) * 100);
        /* Some batteries report POWER_SUPPLY_CHARGE_NOW=<full_design> when fully
         * charged, even though that’s plainly wrong. For people who chose to see
         * the percentage calculated based on the last full capacity, we clamp the
         * value to 100%, as that makes more sense.
         * See http://bugs.debian.org/785398 */
        if (ctx->last_full_capacity && batt_info.percentage_remaining > 100) {
            batt_info.percentage_remaining = 100;
        }
    }

    if (batt_info.seconds_remaining < 0 && batt_info.present_rate > 0 && batt_info.status != CS_FULL) {
        if (batt_info.status == CS_CHARGING)
            batt_info.seconds_remaining = 3600.0 * (full - batt_info.remaining) / batt_info.present_rate;
        else if (batt_info.status == CS_DISCHARGING)
            batt_info.seconds_remaining = 3600.0 * batt_info.remaining / batt_info.present_rate;
        else
            batt_info.seconds_remaining = 0;
    }

    if (batt_info.status == CS_DISCHARGING && ctx->low_threshold > 0) {
        if (batt_info.percentage_remaining >= 0 && strcasecmp(ctx->threshold_type, "percentage") == 0 && batt_info.percentage_remaining < ctx->low_threshold) {
            START_COLOR("color_bad");
            colorful_output = true;
        } else if (batt_info.seconds_remaining >= 0 && strcasecmp(ctx->threshold_type, "time") == 0 && batt_info.seconds_remaining < 60 * ctx->low_threshold) {
            START_COLOR("color_bad");
            colorful_output = true;
        }
    }

    char string_status[STRING_SIZE];
    char string_percentage[STRING_SIZE];
    // following variables are not alwasy set. If they are not set they should be empty.
    char string_remaining[STRING_SIZE] = "";
    char string_emptytime[STRING_SIZE] = "";
    char string_consumption[STRING_SIZE] = "";

    const char *statusstr;
    switch (batt_info.status) {
        case CS_CHARGING:
            statusstr = ctx->status_chr;
            break;
        case CS_DISCHARGING:
            statusstr = ctx->status_bat;
            break;
        case CS_FULL:
            statusstr = ctx->status_full;
            break;
        case CS_IDLE:
            statusstr = ctx->status_idle;
            break;
        default:
            statusstr = ctx->status_unk;
    }
    snprintf(string_status, STRING_SIZE, "%s", statusstr);
    snprintf(string_percentage, STRING_SIZE, ctx->format_percentage, batt_info.percentage_remaining, "%");

    if (batt_info.seconds_remaining >= 0) {
        int seconds, hours, minutes;
        hours = batt_info.seconds_remaining / 3600;
        seconds = batt_info.seconds_remaining - (hours * 3600);
        minutes = seconds / 60;
        seconds -= (minutes * 60);
        if (ctx->hide_seconds)
            snprintf(string_remaining, STRING_SIZE, "%02d:%02d", max(hours, 0), max(minutes, 0));
        else
            snprintf(string_remaining, STRING_SIZE, "%02d:%02d:%02d", max(hours, 0), max(minutes, 0), max(seconds, 0));
    }

    if (batt_info.seconds_remaining >= 0) {
        time_t empty_time = time(NULL) + batt_info.seconds_remaining;
        set_timezone(NULL); /* Use local time. */
        struct tm *empty_tm = localtime(&empty_time);
        if (ctx->hide_seconds)
            snprintf(string_emptytime, STRING_SIZE, "%02d:%02d", max(empty_tm->tm_hour, 0), max(empty_tm->tm_min, 0));
        else
            snprintf(string_emptytime, STRING_SIZE, "%02d:%02d:%02d", max(empty_tm->tm_hour, 0), max(empty_tm->tm_min, 0), max(empty_tm->tm_sec, 0));
    }

    if (batt_info.present_rate >= 0)
        snprintf(string_consumption, STRING_SIZE, "%1.2fW", batt_info.present_rate / 1e6);

    placeholder_t placeholders[] = {
        {.name = "%status", .value = string_status},
        {.name = "%percentage", .value = string_percentage},
        {.name = "%remaining", .value = string_remaining},
        {.name = "%emptytime", .value = string_emptytime},
        {.name = "%consumption", .value = string_consumption}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *untrimmed = format_placeholders(ctx->format, &placeholders[0], num);
    char *formatted = trim(untrimmed);
    OUTPUT_FORMATTED;
    free(formatted);
    free(untrimmed);

    if (colorful_output) {
        END_COLOR;
    }

    OUTPUT_FULL_TEXT(ctx->buf);
}
