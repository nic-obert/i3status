/*
 * i3status – Generates a status line
 *
 * Copyright © 2008 Michael Stapelberg and contributors
 * Copyright © 2009 Thorsten Toepper <atsutane at freethoughts dot de>
 * Copyright © 2010 Axel Wagner <mail at merovius dot de>
 * Copyright © 2010 Fernando Tarlá Cardoso Lemos <fernandotcl at gmail dot com>
 *
 * See file LICENSE for license information.
 *
 */
#include <config.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>
#include <signal.h>
#include <confuse.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <locale.h>

#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define CFG_COLOR_OPTS(good, degraded, bad)             \
    CFG_STR("color_good", good, CFGF_NONE),             \
        CFG_STR("color_degraded", degraded, CFGF_NONE), \
        CFG_STR("color_bad", bad, CFGF_NONE)

#define CFG_CUSTOM_COLOR_OPTS CFG_COLOR_OPTS(NULL, NULL, NULL)

typedef enum {
    ITEM_NONE,
    ITEM_IPV6,
    ITEM_WIRELESS,
    ITEM_ETHERNET,
    ITEM_BATTERY,
    ITEM_LOAD,
    ITEM_MEMORY,
    ITEM_TZTIME,
    ITEM_VOLUME,
    ITEM_CPU_TEMPERATURE,
    ITEM_CPU_USAGE,
} item_type_t;

typedef struct {
    item_type_t type;
    const char *title;
    union {
        ipv6_info_ctx_t ipv6;
        wireless_info_ctx_t wireless;
        eth_info_ctx_t ethernet;
        battery_info_ctx_t battery;
        load_ctx_t load;
        memory_ctx_t memory;
        time_ctx_t tztime;
        volume_ctx_t volume;
        cpu_temperature_ctx_t cpu_temperature;
        cpu_usage_ctx_t cpu_usage;
    } ctx;
} status_item_t;

/* socket file descriptor for general purposes */
int general_socket;

static bool exit_upon_signal = false;
static bool run_once = false;

cfg_t *cfg, *cfg_general, *cfg_section;

pthread_t main_thread;

/*
 * Set the exit_upon_signal flag, because one cannot do anything in a safe
 * manner in a signal handler (e.g. fprintf, which we really want to do for
 * debugging purposes), see
 * https://www.securecoding.cert.org/confluence/display/seccode/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers
 *
 */
void fatalsig(int signum) {
    exit_upon_signal = true;
}

/*
 * Do nothing upon SIGUSR1. Running this signal handler will nevertheless
 * interrupt nanosleep() so that i3status immediately generates new output.
 *
 */
void sigusr1(int signum) {
}

/*
 * Checks if the given path exists by calling stat().
 *
 */
static bool path_exists(const char *path) {
    struct stat buf;
    return (stat(path, &buf) == 0);
}

/*
 * Validates a color in "#RRGGBB" format
 *
 */
static int valid_color(const char *value) {
    const int len = strlen(value);

    if (len != strlen("#rrggbb")) {
        return 0;
    }
    if (value[0] != '#')
        return 0;
    for (int i = 1; i < len; ++i) {
        if (value[i] >= '0' && value[i] <= '9')
            continue;
        if (value[i] >= 'a' && value[i] <= 'f')
            continue;
        if (value[i] >= 'A' && value[i] <= 'F')
            continue;
        return 0;
    }
    return 1;
}

static char *get_config_path(void) {
    char *xdg_config_home, *xdg_config_dirs, *config_path;

    /* 1: check for $XDG_CONFIG_HOME/i3status/config */
    if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL)
        xdg_config_home = "~/.config";

    xdg_config_home = resolve_tilde(xdg_config_home);
    if (asprintf(&config_path, "%s/i3status/config", xdg_config_home) == -1)
        die("asprintf() failed\n");
    free(xdg_config_home);

    if (path_exists(config_path))
        return config_path;
    free(config_path);

    /* 2: check for $XDG_CONFIG_DIRS/i3status/config */
    if ((xdg_config_dirs = getenv("XDG_CONFIG_DIRS")) == NULL)
        xdg_config_dirs = "/etc/xdg";

    /* 3: check the traditional path under the home directory */
    config_path = resolve_tilde("~/.i3status.conf");
    if (path_exists(config_path))
        return config_path;
    free(config_path);
    char *buf = strdup(xdg_config_dirs);
    char *tok = strtok(buf, ":");
    while (tok != NULL) {
        tok = resolve_tilde(tok);
        if (asprintf(&config_path, "%s/i3status/config", tok) == -1)
            die("asprintf() failed\n");
        free(tok);
        if (path_exists(config_path)) {
            free(buf);
            return config_path;
        }
        free(config_path);
        tok = strtok(NULL, ":");
    }
    free(buf);

    /* 4: check the traditional path under /etc */
    config_path = SYSCONFDIR "/i3status.conf";
    if (path_exists(config_path))
        return sstrdup(config_path);

    die("Unable to find the configuration file (looked at "
        "~/.i3status.conf, $XDG_CONFIG_HOME/i3status/config, " SYSCONFDIR "/i3status.conf and $XDG_CONFIG_DIRS/i3status/config)");
}

int main(int argc, char *argv[]) {
    unsigned int j;

    cfg_opt_t general_opts[] = {
        CFG_BOOL("colors", 1, CFGF_NONE),
        CFG_STR("separator", "default", CFGF_NONE),
        CFG_STR("color_separator", "#333333", CFGF_NONE),
        CFG_INT("interval", 1, CFGF_NONE),
        CFG_COLOR_OPTS("#00FF00", "#FFFF00", "#FF0000"),
        CFG_END()};

    cfg_opt_t wireless_opts[] = {
        CFG_STR("format_up", "W: (%quality at %essid, %bitrate) %ip", CFGF_NONE),
        CFG_STR("format_down", "W: down", CFGF_NONE),
        CFG_STR("format_bitrate", "%g %cb/s", CFGF_NONE),
        CFG_STR("format_noise", "%3d%s", CFGF_NONE),
        CFG_STR("format_quality", "%3d%s", CFGF_NONE),
        CFG_STR("format_signal", "%3d%s", CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t ethernet_opts[] = {
        CFG_STR("format_up", "E: %ip (%speed)", CFGF_NONE),
        CFG_STR("format_down", "E: down", CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t ipv6_opts[] = {
        CFG_STR("format_up", "%ip", CFGF_NONE),
        CFG_STR("format_down", "no IPv6", CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t battery_opts[] = {
        CFG_STR("format", "%status %percentage %remaining", CFGF_NONE),
        CFG_STR("format_down", "No battery", CFGF_NONE),
        CFG_STR("format_percentage", "%.02f%s", CFGF_NONE),
        CFG_STR("status_chr", "CHR", CFGF_NONE),
        CFG_STR("status_bat", "BAT", CFGF_NONE),
        CFG_STR("status_unk", "UNK", CFGF_NONE),
        CFG_STR("status_full", "FULL", CFGF_NONE),
        CFG_STR("status_idle", "IDLE", CFGF_NONE),
        CFG_STR("path", "/sys/class/power_supply/BAT%d/uevent", CFGF_NONE),
        CFG_INT("low_threshold", 30, CFGF_NONE),
        CFG_STR("threshold_type", "time", CFGF_NONE),
        CFG_BOOL("last_full_capacity", false, CFGF_NONE),
        CFG_BOOL("integer_battery_capacity", false, CFGF_NONE),
        CFG_BOOL("hide_seconds", true, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t tztime_opts[] = {
        CFG_STR("format", "%Y-%m-%d %H:%M:%S %Z", CFGF_NONE),
        CFG_STR("timezone", "", CFGF_NONE),
        CFG_STR("locale", "", CFGF_NONE),
        CFG_STR("format_time", NULL, CFGF_NONE),
        CFG_BOOL("hide_if_equals_localtime", false, CFGF_NONE),
        CFG_END()};

    cfg_opt_t load_opts[] = {
        CFG_STR("format", "%1min %5min %15min", CFGF_NONE),
        CFG_STR("format_above_threshold", NULL, CFGF_NONE),
        CFG_FLOAT("max_threshold", 5, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t memory_opts[] = {
        CFG_STR("format", "%used %free %available", CFGF_NONE),
        CFG_STR("format_degraded", NULL, CFGF_NONE),
        CFG_STR("threshold_degraded", NULL, CFGF_NONE),
        CFG_STR("threshold_critical", NULL, CFGF_NONE),
        CFG_STR("memory_used_method", "classical", CFGF_NONE),
        CFG_STR("unit", "auto", CFGF_NONE),
        CFG_INT("decimals", 1, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t usage_opts[] = {
        CFG_STR("format", "%usage", CFGF_NONE),
        CFG_STR("format_above_threshold", NULL, CFGF_NONE),
        CFG_STR("format_above_degraded_threshold", NULL, CFGF_NONE),
        CFG_STR("path", "/proc/stat", CFGF_NONE),
        CFG_FLOAT("max_threshold", 95, CFGF_NONE),
        CFG_FLOAT("degraded_threshold", 90, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t temp_opts[] = {
        CFG_STR("format", "%degrees C", CFGF_NONE),
        CFG_STR("format_above_threshold", NULL, CFGF_NONE),
        CFG_STR("path", NULL, CFGF_NONE),
        CFG_INT("max_threshold", 75, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t volume_opts[] = {
        CFG_STR("format", "♪: %volume", CFGF_NONE),
        CFG_STR("format_muted", "♪: 0%%", CFGF_NONE),
        CFG_STR("device", "default", CFGF_NONE),
        CFG_STR("mixer", "Master", CFGF_NONE),
        CFG_INT("mixer_idx", 0, CFGF_NONE),
        CFG_CUSTOM_COLOR_OPTS,
        CFG_END()};

    cfg_opt_t opts[] = {
        CFG_STR_LIST("order", "{}", CFGF_NONE),
        CFG_SEC("general", general_opts, CFGF_NONE),
        CFG_SEC("wireless", wireless_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("ethernet", ethernet_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("battery", battery_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("cpu_temperature", temp_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("volume", volume_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("ipv6", ipv6_opts, CFGF_NONE),
        CFG_SEC("tztime", tztime_opts, CFGF_TITLE | CFGF_MULTI),
        CFG_SEC("load", load_opts, CFGF_NONE),
        CFG_SEC("memory", memory_opts, CFGF_NONE),
        CFG_SEC("cpu_usage", usage_opts, CFGF_NONE),
        CFG_END()};

    char *configfile = NULL;
    int opt, option_index = 0;
    struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"run-once", no_argument, 0, 0},
        {0, 0, 0, 0}};

    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = fatalsig;
    main_thread = pthread_self();

    /* Exit upon SIGPIPE because when we have nowhere to write to, gathering system
     * information is pointless. Also exit explicitly on SIGTERM and SIGINT because
     * only this will trigger a reset of the cursor in the terminal output-format.
     */
    sigaction(SIGPIPE, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = sigusr1;
    sigaction(SIGUSR1, &action, NULL);

    if (setlocale(LC_ALL, "") == NULL)
        die("Could not set locale. Please make sure all your LC_* / LANG settings are correct.\n");

    while ((opt = getopt_long(argc, argv, "c:hv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                configfile = optarg;
                break;
            case 'h':
                printf("i3status " I3STATUS_VERSION " © 2008 Michael Stapelberg and contributors\n"
                       "Syntax: %s [-c <configfile>] [-h] [-v] [--run-once]\n",
                       argv[0]);
                return 0;
            case 'v':
                printf("i3status " I3STATUS_VERSION " © 2008 Michael Stapelberg and contributors\n"
                       "Built with pulseaudio support\n"
                );
                return 0;
            case 0:
                if (strcmp(long_options[option_index].name, "run-once") == 0) {
                    run_once = true;
                }
                break;
        }
    }

    if (configfile == NULL)
        configfile = get_config_path();

    cfg = cfg_init(opts, CFGF_NOCASE);
    if (cfg_parse(cfg, configfile) == CFG_PARSE_ERROR)
        return EXIT_FAILURE;

    unsigned int order_size = cfg_size(cfg, "order");
    if (order_size == 0)
        die("Your 'order' array is empty. Please fix your config.\n");

    cfg_general = cfg_getsec(cfg, "general");
    if (cfg_general == NULL)
        die("Could not get section \"general\"\n");

    if (!valid_color(cfg_getstr(cfg_general, "color_good")) || !valid_color(cfg_getstr(cfg_general, "color_degraded")) || !valid_color(cfg_getstr(cfg_general, "color_bad")) || !valid_color(cfg_getstr(cfg_general, "color_separator")))
        die("Bad color format\n");

    yajl_gen json_gen = yajl_gen_alloc(NULL);

    /* Initialize the i3bar protocol. See i3/docs/i3bar-protocol
        * for details. */
    printf("{\"version\":1}\n[\n");
    fflush(stdout);
    yajl_gen_array_open(json_gen);
    yajl_gen_clear(json_gen);

    if ((general_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        die("Could not create socket\n");

    int interval = cfg_getint(cfg_general, "interval");
    if (interval <= 0) {
        die("Invalid interval attribute found in section %s, line %d: %d\n"
            "Expected positive integer\n",
            cfg_general->name, cfg_general->line, interval);
    }

    /* 512 bytes which each plugin can use to buffer output.
     * Even though it’s unclean, we just assume that the user will not
     * specify a format string which expands to something longer than 512
     * bytes
    */
    char buffer[512];

    /*
    Pre-build cfg structs and initialize their static fields.
    This prevents reduntant calls to cfg_* functions.
    */
    cfg_t *_sec;
    _sec = cfg_getsec(cfg, "ipv6");
    ipv6_info_ctx_t ipv6_ctx = {
        .json_gen = json_gen,
        .buf = buffer,
        .buflen = sizeof(buffer),
        .format_up = cfg_getstr(_sec, "format_up"),
        .format_down = cfg_getstr(_sec, "format_down"),
    };
    _sec = cfg_getsec(cfg, "load");
    load_ctx_t load_ctx = {
        .json_gen = json_gen,
        .buf = buffer,
        .buflen = sizeof(buffer),
        .format = cfg_getstr(_sec, "format"),
        .format_above_threshold = cfg_getstr(_sec, "format_above_threshold"),
        .max_threshold = cfg_getfloat(_sec, "max_threshold"),
    };
    _sec = cfg_getsec(cfg, "cpu_usage");
    cpu_usage_ctx_t cpu_usage_ctx = {
        .json_gen = json_gen,
        .buf = buffer,
        .buflen = sizeof(buffer),
        .format = cfg_getstr(_sec, "format"),
        .format_above_threshold = cfg_getstr(_sec, "format_above_threshold"),
        .format_above_degraded_threshold = cfg_getstr(_sec, "format_above_degraded_threshold"),
        .path = cfg_getstr(_sec, "path"),
        .max_threshold = cfg_getfloat(_sec, "max_threshold"),
        .degraded_threshold = cfg_getfloat(_sec, "degraded_threshold"),
    };
    _sec = cfg_getsec(cfg, "memory");
    memory_ctx_t memory_ctx = {
        .json_gen = json_gen,
        .buf = buffer,
        .buflen = sizeof(buffer),
        .format = cfg_getstr(_sec, "format"),
        .format_degraded = cfg_getstr(_sec, "format_degraded"),
        .threshold_degraded = cfg_getstr(_sec, "threshold_degraded"),
        .threshold_critical = cfg_getstr(_sec, "threshold_critical"),
        .memory_used_method = cfg_getstr(_sec, "memory_used_method"),
        .unit = cfg_getstr(_sec, "unit"),
        .decimals = cfg_getint(_sec, "decimals"),
    };
    
    status_item_t *items = calloc(order_size, sizeof(*items));
    if (items == NULL)
        die("calloc() failed\n");

    for (j = 0; j < order_size; j++) {
        const char *current = cfg_getnstr(cfg, "order", j);
        status_item_t *item = &items[j];

        if (BEGINS_WITH(current, "ipv6")) {
            item->type = ITEM_IPV6;
            memcpy(&item->ctx.ipv6, &ipv6_ctx, sizeof(item->ctx.ipv6));
        } else if (BEGINS_WITH(current, "wireless")) {
            item->type = ITEM_WIRELESS;
            item->title = current + strlen("wireless") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "wireless", item->title);
            if (sec == NULL)
                continue;
            memcpy(&item->ctx.wireless, &(wireless_info_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .interface = item->title,
                .format_up = cfg_getstr(sec, "format_up"),
                .format_down = cfg_getstr(sec, "format_down"),
                .format_bitrate = cfg_getstr(sec, "format_bitrate"),
                .format_noise = cfg_getstr(sec, "format_noise"),
                .format_quality = cfg_getstr(sec, "format_quality"),
                .format_signal = cfg_getstr(sec, "format_signal"),
            }, sizeof(item->ctx.wireless));
        } else if (BEGINS_WITH(current, "ethernet")) {
            item->type = ITEM_ETHERNET;
            item->title = current + strlen("ethernet") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "ethernet", item->title);
            if (sec == NULL)
                continue;
            memcpy(&item->ctx.ethernet, &(eth_info_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .interface = item->title,
                .format_up = cfg_getstr(sec, "format_up"),
                .format_down = cfg_getstr(sec, "format_down"),
            }, sizeof(item->ctx.ethernet));
        } else if (BEGINS_WITH(current, "battery")) {
            item->type = ITEM_BATTERY;
            item->title = current + strlen("battery") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "battery", item->title);
            if (sec == NULL)
                continue;
            char *format_percentage = cfg_getstr(sec, "format_percentage");
            if (cfg_getbool(sec, "integer_battery_capacity")) {
                if (strcmp("%.02f%s", format_percentage) == 0)
                    cfg_setstr(sec, "format_percentage", "%.00f%s");
                else
                    fprintf(stderr, "i3status: integer_battery_capacity is deprecated\n");
            }
            memcpy(&item->ctx.battery, &(battery_info_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .number = strcasecmp(item->title, "all") == 0 ? -1 : atoi(item->title),
                .path = cfg_getstr(sec, "path"),
                .format = cfg_getstr(sec, "format"),
                .format_down = cfg_getstr(sec, "format_down"),
                .status_chr = cfg_getstr(sec, "status_chr"),
                .status_bat = cfg_getstr(sec, "status_bat"),
                .status_unk = cfg_getstr(sec, "status_unk"),
                .status_full = cfg_getstr(sec, "status_full"),
                .status_idle = cfg_getstr(sec, "status_idle"),
                .low_threshold = cfg_getint(sec, "low_threshold"),
                .threshold_type = cfg_getstr(sec, "threshold_type"),
                .last_full_capacity = cfg_getbool(sec, "last_full_capacity"),
                .format_percentage = cfg_getstr(sec, "format_percentage"),
                .hide_seconds = cfg_getbool(sec, "hide_seconds"),
            }, sizeof(item->ctx.battery));
        } else if (BEGINS_WITH(current, "load")) {
            item->type = ITEM_LOAD;
            memcpy(&item->ctx.load, &load_ctx, sizeof(item->ctx.load));
        } else if (BEGINS_WITH(current, "memory")) {
            item->type = ITEM_MEMORY;
            memcpy(&item->ctx.memory, &memory_ctx, sizeof(item->ctx.memory));
        } else if (BEGINS_WITH(current, "tztime")) {
            item->type = ITEM_TZTIME;
            item->title = current + strlen("tztime") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "tztime", item->title);
            if (sec == NULL)
                continue;
            memcpy(&item->ctx.tztime, &(time_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .title = item->title,
                .format = cfg_getstr(sec, "format"),
                .tz = cfg_getstr(sec, "timezone"),
                .locale = cfg_getstr(sec, "locale"),
                .format_time = cfg_getstr(sec, "format_time"),
                .hide_if_equals_localtime = cfg_getbool(sec, "hide_if_equals_localtime"),
            }, sizeof(item->ctx.tztime));
        } else if (BEGINS_WITH(current, "volume")) {
            item->type = ITEM_VOLUME;
            item->title = current + strlen("volume") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "volume", item->title);
            if (sec == NULL)
                continue;
            memcpy(&item->ctx.volume, &(volume_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .fmt = cfg_getstr(sec, "format"),
                .fmt_muted = cfg_getstr(sec, "format_muted"),
                .device = cfg_getstr(sec, "device"),
                .mixer = cfg_getstr(sec, "mixer"),
                .mixer_idx = cfg_getint(sec, "mixer_idx"),
            }, sizeof(item->ctx.volume));
        } else if (BEGINS_WITH(current, "cpu_temperature")) {
            item->type = ITEM_CPU_TEMPERATURE;
            item->title = current + strlen("cpu_temperature") + 1;
            cfg_t *sec = cfg_gettsec(cfg, "cpu_temperature", item->title);
            if (sec == NULL)
                continue;
            memcpy(&item->ctx.cpu_temperature, &(cpu_temperature_ctx_t){
                .json_gen = json_gen,
                .buf = buffer,
                .buflen = sizeof(buffer),
                .zone = atoi(item->title),
                .path = cfg_getstr(sec, "path"),
                .format = cfg_getstr(sec, "format"),
                .format_above_threshold = cfg_getstr(sec, "format_above_threshold"),
                .max_threshold = cfg_getint(sec, "max_threshold"),
            }, sizeof(item->ctx.cpu_temperature));
        } else if (BEGINS_WITH(current, "cpu_usage")) {
            item->type = ITEM_CPU_USAGE;
            memcpy(&item->ctx.cpu_usage, &cpu_usage_ctx, sizeof(item->ctx.cpu_usage));
        }
    }

    while (1) {
        if (exit_upon_signal) {
            fprintf(stderr, "i3status: exiting due to signal.\n");
            exit(1);
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        yajl_gen_array_open(json_gen);
        for (j = 0; j < order_size; j++) {
            status_item_t *item = &items[j];

            switch (item->type) {
            case ITEM_IPV6:
                SEC_OPEN_MAP("ipv6");
                print_ipv6_info(&item->ctx.ipv6);
                SEC_CLOSE_MAP;
                break;

            case ITEM_WIRELESS:
                if (strcasecmp(item->title, "_first_") == 0)
                    item->ctx.wireless.interface =
                        first_eth_interface(NET_TYPE_WIRELESS);
                if (item->ctx.wireless.interface == NULL)
                    item->ctx.wireless.interface = item->title;
                SEC_OPEN_MAP("wireless");
                print_wireless_info(&item->ctx.wireless);
                SEC_CLOSE_MAP;
                break;

            case ITEM_ETHERNET:
                if (strcasecmp(item->title, "_first_") == 0)
                    item->ctx.ethernet.interface =
                        first_eth_interface(NET_TYPE_ETHERNET);
                if (item->ctx.ethernet.interface == NULL)
                    item->ctx.ethernet.interface = item->title;
                SEC_OPEN_MAP("ethernet");
                print_eth_info(&item->ctx.ethernet);
                SEC_CLOSE_MAP;
                break;

            case ITEM_BATTERY:
                SEC_OPEN_MAP("battery");
                print_battery_info(&item->ctx.battery);
                SEC_CLOSE_MAP;
                break;

            case ITEM_LOAD:
                SEC_OPEN_MAP("load");
                print_load(&item->ctx.load);
                SEC_CLOSE_MAP;
                break;

            case ITEM_MEMORY:
                SEC_OPEN_MAP("memory");
                print_memory(&item->ctx.memory);
                SEC_CLOSE_MAP;
                break;

            case ITEM_TZTIME:
                SEC_OPEN_MAP("tztime");
                item->ctx.tztime.t = tv.tv_sec;
                print_time(&item->ctx.tztime);
                SEC_CLOSE_MAP;
                break;

            case ITEM_VOLUME:
                SEC_OPEN_MAP("volume");
                print_volume(&item->ctx.volume);
                SEC_CLOSE_MAP;
                break;

            case ITEM_CPU_TEMPERATURE:
                SEC_OPEN_MAP("cpu_temperature");
                print_cpu_temperature_info(&item->ctx.cpu_temperature);
                SEC_CLOSE_MAP;
                break;

            case ITEM_CPU_USAGE:
                SEC_OPEN_MAP("cpu_usage");
                print_cpu_usage(&item->ctx.cpu_usage);
                SEC_CLOSE_MAP;
                break;

            case ITEM_NONE:
                break;
            }
        }
        yajl_gen_array_close(json_gen);
        const unsigned char *buf;
        size_t len;
        yajl_gen_get_buf(json_gen, &buf, &len);
        write(STDOUT_FILENO, buf, len);
        yajl_gen_clear(json_gen);

        printf("\n");
        fflush(stdout);

        if (run_once) {
            break;
        }

        /* To provide updates on every full second (as good as possible)
         * we don’t use sleep(interval) but we sleep until the next
         * second (with microsecond precision) plus (interval-1)
         * seconds. We also align to 60 seconds modulo interval such
         * that we start with :00 on every new minute. */
        struct timeval current_timeval;
        gettimeofday(&current_timeval, NULL);
        struct timespec ts = {interval - 1 - (current_timeval.tv_sec % interval), (10e5 - current_timeval.tv_usec) * 1000};
        nanosleep(&ts, NULL);
    }

    yajl_gen_free(json_gen);
}
