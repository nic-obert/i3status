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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <locale.h>

#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"


/* socket file descriptor for general purposes */
int general_socket;

static bool exit_upon_signal = false;
static bool run_once = false;

general_opts_t *general_cfg;

pthread_t main_thread;

const size_t PLUGIN_BUF_LEN = 512;

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

static char *get_config_path(void) {
    char *xdg_config_home, *xdg_config_dirs, *config_path;

    /* 1: check for $XDG_CONFIG_HOME/i3status/config.json */
    if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL)
        xdg_config_home = "~/.config";

    xdg_config_home = resolve_tilde(xdg_config_home);
    if (asprintf(&config_path, "%s/i3status/config.json", xdg_config_home) == -1)
        die("asprintf() failed\n");
    free(xdg_config_home);

    if (path_exists(config_path))
        return config_path;
    free(config_path);

    /* 2: check for $XDG_CONFIG_DIRS/i3status/config.json */
    if ((xdg_config_dirs = getenv("XDG_CONFIG_DIRS")) == NULL)
        xdg_config_dirs = "/etc/xdg";

    /* 3: check the traditional path under the home directory */
    config_path = resolve_tilde("~/.i3status.json");
    if (path_exists(config_path))
        return config_path;
    free(config_path);
    char *buf = strdup(xdg_config_dirs);
    char *tok = strtok(buf, ":");
    while (tok != NULL) {
        tok = resolve_tilde(tok);
        if (asprintf(&config_path, "%s/i3status/config.json", tok) == -1)
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
    config_path = SYSCONFDIR "/i3status.json";
    if (path_exists(config_path))
        return sstrdup(config_path);

    die("Unable to find the configuration file (looked at "
        "~/.i3status.json, $XDG_CONFIG_HOME/i3status/config.json, " SYSCONFDIR "/i3status.json and $XDG_CONFIG_DIRS/i3status/config.json)");
}


int main(int argc, char *argv[]) {

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

    // Parse CLI arguments
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

    // Parse configuration file
    config_t cfg;
    if (config_parse(configfile, &cfg) != 0)
        return EXIT_FAILURE;
    general_cfg = &cfg.general;

    if (cfg.item_count == 0)
        die("Your 'order' array is empty. Please fix your config.\n");

    yajl_gen json_gen = yajl_gen_alloc(NULL);

    /* Initialize the i3bar protocol. See i3/docs/i3bar-protocol
        * for details. */
    printf("{\"version\":1}\n[\n");
    fflush(stdout);
    yajl_gen_array_open(json_gen);
    yajl_gen_clear(json_gen);

    if ((general_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        die("Could not create socket\n");

    unsigned int interval = general_cfg->interval;

    /* A buffer which each plugin can use to buffer output.
     * Even though it’s unclean, we just assume that the user will not specify
     * a format string which expands to something bigger than the chosen buffer
    */
    char buffer[PLUGIN_BUF_LEN];

    output_ctx_t out_ctx = {
        .buf = buffer,
        .json_gen = json_gen
    };

    while (1) {
        if (exit_upon_signal) {
            fprintf(stderr, "i3status: exiting due to signal.\n");
            exit(1);
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        yajl_gen_array_open(json_gen);
        for (unsigned int j = 0; j < cfg.item_count; j++) {
            status_item_t *item = &cfg.items[j];

            switch (item->type) {
            case ITEM_IPV6: // TODO: what is this? How is it different from print_wireless_info? Cant' we just introduce an option to prefer ipv6 over ipv4?
                SEC_OPEN_MAP("ipv6");
                print_ipv6_info(&item->ctx.ipv6, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_WIRELESS:
                if (item->ctx.wireless.use_first)
                    item->ctx.wireless.interface = first_net_interface(NET_TYPE_WIRELESS);
                SEC_OPEN_MAP("wireless");
                print_wireless_info(&item->ctx.wireless, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_ETHERNET:
                if (item->ctx.ethernet.use_first)
                    item->ctx.ethernet.interface = first_net_interface(NET_TYPE_ETHERNET);
                SEC_OPEN_MAP("ethernet");
                print_eth_info(&item->ctx.ethernet, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_BATTERY:
                SEC_OPEN_MAP("battery");
                print_battery_info(&item->ctx.battery, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_LOAD:
                SEC_OPEN_MAP("load");
                print_load(&item->ctx.load, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_MEMORY:
                SEC_OPEN_MAP("memory");
                print_memory(&item->ctx.memory, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_TZTIME:
                SEC_OPEN_MAP("tztime");
                item->ctx.tztime.t = tv.tv_sec;
                print_time(&item->ctx.tztime, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_VOLUME:
                SEC_OPEN_MAP("volume");
                print_volume(&item->ctx.volume, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_CPU_TEMPERATURE:
                SEC_OPEN_MAP("cpu_temperature");
                print_cpu_temperature_info(&item->ctx.cpu_temperature, &out_ctx);
                SEC_CLOSE_MAP;
                break;

            case ITEM_CPU_USAGE:
                SEC_OPEN_MAP("cpu_usage");
                print_cpu_usage(&item->ctx.cpu_usage, &out_ctx);
                SEC_CLOSE_MAP;
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

        struct timespec ts = {
            interval / 1000,
            (interval % 1000) * 1000000L
        };
        nanosleep(&ts, NULL);
    }

    yajl_gen_free(json_gen);
}
