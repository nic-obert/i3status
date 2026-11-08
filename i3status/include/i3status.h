#ifndef _I3STATUS_H
#define _I3STATUS_H

#include <stdbool.h>
#include <time.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <ifaddrs.h>


#define BEGINS_WITH(haystack, needle) (strncmp(haystack, needle, strlen(needle)) == 0)
#define max(a, b) ((a) > (b) ? (a) : (b))

#define DEFAULT_SINK_INDEX UINT32_MAX
#define COMPOSE_VOLUME_MUTE(vol, mute) ((vol) | ((mute) ? (1 << 30) : 0))
#define DECOMPOSE_VOLUME(cvol) ((cvol) & ~(1 << 30))
#define DECOMPOSE_MUTED(cvol) (((cvol) & (1 << 30)) != 0)
#define MAX_SINK_DESCRIPTION_LEN (128) /* arbitrary */

#define THERMAL_ZONE "/sys/class/thermal/thermal_zone%d/temp"

/* Macro which any plugin can use to output the full_text part (when the output
 * format is JSON) or just output to stdout (any other output format). */
#define OUTPUT_FULL_TEXT(text)                                                                       \
    do {                                                                                             \
        /* Terminate the output buffer here in any case, so that it’s                                \
         * not forgotten in the module */                                                            \
        *outwalk = '\0';                                                                             \
        yajl_gen_string(out_ctx->json_gen, (const unsigned char *)"full_text", strlen("full_text")); \
        yajl_gen_string(out_ctx->json_gen, (const unsigned char *)text, strlen(text));               \
    } while (0)

#define SEC_OPEN_MAP(name)                                                            \
    do {                                                                              \
        yajl_gen_map_open(json_gen);                                              \
        yajl_gen_string(json_gen, (const unsigned char *)"name", strlen("name")); \
        yajl_gen_string(json_gen, (const unsigned char *)name, strlen(name));     \
    } while (0)

#define SEC_CLOSE_MAP                                                                                 \
    do {                                                                                              \
        if (strlen(general_cfg->separator) == 0) {                                                    \
            yajl_gen_string(json_gen, (const unsigned char *)"separator", strlen("separator"));       \
            yajl_gen_bool(json_gen, false);                                                           \
        }                                                                                             \
        yajl_gen_map_close(json_gen);                                                                 \
    } while (0)

#define START_COLOR_VAR(color)                                                                   \
    do {                                                                                         \
        if (general_cfg->colors) {                                                               \
            yajl_gen_string(out_ctx->json_gen, (const unsigned char *)"color", strlen("color")); \
            yajl_gen_string(out_ctx->json_gen, (const unsigned char *)color, strlen(color));     \
        }                                                                                        \
    } while (0)

#define START_COLOR(color)                                                                       \
    do {                                                                                         \
        if (general_cfg->colors) {                                                               \
            yajl_gen_string(out_ctx->json_gen, (const unsigned char *)"color", strlen("color")); \
            yajl_gen_string(out_ctx->json_gen, (const unsigned char *)general_cfg->color_opts.color, strlen(general_cfg->color_opts.color));       \
        }                                                                                        \
    } while (0)

#define INSTANCE(instance)                                                                         \
    do {                                                                                           \
        yajl_gen_string(out_ctx->json_gen, (const unsigned char *)"instance", strlen("instance")); \
        yajl_gen_string(out_ctx->json_gen, (const unsigned char *)instance, strlen(instance));     \
    } while (0)

#define OUTPUT_FORMATTED                                             \
    do {                                                             \
        const size_t remaining = PLUGIN_BUF_LEN - (outwalk - out_ctx->buf); \
        strncpy(outwalk, formatted, remaining);                      \
        outwalk += strlen(formatted);                                \
    } while (0)


typedef struct {
    yajl_gen json_gen;
    char* buf;
} output_ctx_t;

/* src/general.c */
char *skip_character(char *input, char character, int amount);

void die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
bool slurp(const char *filename, char *destination, int size);
char *resolve_tilde(const char *path);
void *scalloc(size_t size);
char *sstrdup(const char *str);

/* src/output.c */
char *color(const char *colorstr);
void reset_cursor(void);

char *rtrim(const char *s);
char *ltrim(const char *s);
char *trim(const char *s);

// copied from  i3:libi3/format_placeholders.c
/* src/format_placeholders.c */
typedef struct {
    /* The placeholder to be replaced, e.g., "%title". */
    const char *name;
    /* The value this placeholder should be replaced with. */
    const char *value;
} placeholder_t;
char *format_placeholders(const char *format, placeholder_t *placeholders, int num);

/* src/print_time.c */
void set_timezone(const char *tz);

/* src/first_network_device.c */
typedef enum {
    NET_TYPE_WIRELESS = 0,
    NET_TYPE_ETHERNET = 1,
    NET_TYPE_OTHER = 2
} net_type_t;
const char *first_net_interface(const net_type_t type, const struct ifaddrs *ifaddr);

typedef struct {
    const char *format_up;
    const char *format_down;
} ipv6_info_ctx_t;

void print_ipv6_info(ipv6_info_ctx_t *ctx, output_ctx_t *out_ctx);

typedef enum : uint8_t {
    PERCENTAGE,
    TIME
} bat_threshold_type_t;

typedef struct {
    const char *path;
    const char *format;
    const char *format_down;
    const char *status_chr;
    const char *status_bat;
    const char *status_unk;
    const char *status_full;
    const char *status_idle;
    const char *format_percentage;
    int number;
    bat_threshold_type_t threshold_type;
    uint8_t low_threshold;
    bool last_full_capacity;
    bool hide_seconds;
} battery_info_ctx_t;

void print_battery_info(battery_info_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    const char *title;
    const char *format;
    const char *tz;
    const char *locale;
    const char *format_time;
    time_t t;
    bool hide_if_equals_localtime;
} time_ctx_t;

void print_time(time_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    char *ipv4_address;
    char *ipv6_address;
} ip_addr_t;

ip_addr_t get_ip_addr_ipv4_ipv6(const char *interface, const struct ifaddrs *ifaddr);

typedef struct {
    const char *interface;
    const char *format_up;
    const char *format_down;
    const char *format_bitrate;
    const char *format_noise;
    const char *format_quality;
    const char *format_signal;
    bool use_first;
} wireless_info_ctx_t;

void print_wireless_info(wireless_info_ctx_t *ctx, output_ctx_t *out_ctx, const struct ifaddrs *ifaddr);
void free_wireless_info_cache(void);

typedef struct {
    const char *path;
    const char *format;
    const char *format_above_threshold;
    int zone;
    int max_threshold;
} cpu_temperature_ctx_t;

void print_cpu_temperature_info(cpu_temperature_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    const char *format;
    const char *format_above_threshold;
    const char *format_above_degraded_threshold;
    const char *path;
    float max_threshold;
    float degraded_threshold;
} cpu_usage_ctx_t;

void print_cpu_usage(cpu_usage_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    const char *interface;
    const char *format_up;
    const char *format_down;
    bool use_first;
} eth_info_ctx_t;

void print_eth_info(eth_info_ctx_t *ctx, output_ctx_t *out_ctx, const struct ifaddrs *ifaddr);

typedef struct {
    const char *format;
    const char *format_above_threshold;
    float max_threshold;
} load_ctx_t;

void print_load(load_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    const char *format;
    const char *format_degraded;
    const char *threshold_degraded;
    const char *threshold_critical;
    const char *memory_used_method;
    const char *unit;
    int decimals;
} memory_ctx_t;

void print_memory(memory_ctx_t *ctx, output_ctx_t *out_ctx);

typedef struct {
    const char *fmt;
    const char *fmt_muted;
    const char *device;
    const char *mixer;
    int mixer_idx;
} volume_ctx_t;

void print_volume(volume_ctx_t *ctx, output_ctx_t *out_ctx);

int volume_pulseaudio(uint32_t sink_idx, const char *sink_name);
bool description_pulseaudio(uint32_t sink_idx, const char *sink_name, char buffer[MAX_SINK_DESCRIPTION_LEN]);
bool pulse_initialize(void);

/* config */

typedef enum {
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

typedef struct {
    char good[8];
    char degraded[8];
    char bad[8];
} color_opts_t;

typedef struct {
    color_opts_t color_opts;
    const char *separator;
    char color_separator[8];
    unsigned int interval;
    bool colors;
} general_opts_t;

typedef struct {
    general_opts_t general;
    status_item_t *items;
    unsigned int item_count;
} config_t;

int config_parse(const char *filename, config_t *output);

/* socket file descriptor for general purposes */
extern int general_socket;

extern general_opts_t *general_cfg;

extern pthread_t main_thread;

extern const size_t PLUGIN_BUF_LEN;

#endif
