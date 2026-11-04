#ifndef _I3STATUS_H
#define _I3STATUS_H

#include <stdbool.h>
#include <confuse.h>
#include <time.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>


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
        /* Terminate the output buffer here in any case, so that it’s                              \
         * not forgotten in the module */                                                            \
        *outwalk = '\0';                                                                             \
        yajl_gen_string(ctx->json_gen, (const unsigned char *)"full_text", strlen("full_text")); \
        yajl_gen_string(ctx->json_gen, (const unsigned char *)text, strlen(text));               \
    } while (0)

#define SEC_OPEN_MAP(name)                                                            \
    do {                                                                              \
        yajl_gen_map_open(json_gen);                                              \
        yajl_gen_string(json_gen, (const unsigned char *)"name", strlen("name")); \
        yajl_gen_string(json_gen, (const unsigned char *)name, strlen(name));     \
    } while (0)

#define SEC_CLOSE_MAP                                                                                                       \
    do {                                                                                                                    \
        const char *_sep = cfg_getstr(cfg_general, "separator");                                                        \
        if (strlen(_sep) == 0) {                                                                                        \
            yajl_gen_string(json_gen, (const unsigned char *)"separator", strlen("separator"));                         \
            yajl_gen_bool(json_gen, false);                                                                             \
        }                                                                                                               \
        yajl_gen_map_close(json_gen);                                                                                   \
    } while (0)

#define START_COLOR(colorstr)                                                                    \
    do {                                                                                         \
        if (cfg_getbool(cfg_general, "colors")) {                                                \
            const char *_val = NULL;                                                             \
            if (cfg_section)                                                                     \
                _val = cfg_getstr(cfg_section, colorstr);                                        \
            if (!_val)                                                                           \
                _val = cfg_getstr(cfg_general, colorstr);                                        \
            yajl_gen_string(ctx->json_gen, (const unsigned char *)"color", strlen("color")); \
            yajl_gen_string(ctx->json_gen, (const unsigned char *)_val, strlen(_val));       \
        }                                                                                        \
    } while (0)

#define INSTANCE(instance)                                                                         \
    do {                                                                                           \
        yajl_gen_string(ctx->json_gen, (const unsigned char *)"instance", strlen("instance")); \
        yajl_gen_string(ctx->json_gen, (const unsigned char *)instance, strlen(instance));     \
    } while (0)

#define OUTPUT_FORMATTED                                             \
    do {                                                             \
        const size_t remaining = ctx->buflen - (outwalk - ctx->buf); \
        strncpy(outwalk, formatted, remaining);                      \
        outwalk += strlen(formatted);                                \
    } while (0)

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
const char *first_eth_interface(const net_type_t type);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *format_up;
    const char *format_down;
} ipv6_info_ctx_t;

void print_ipv6_info(ipv6_info_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    int number;
    const char *path;
    const char *format;
    const char *format_down;
    const char *status_chr;
    const char *status_bat;
    const char *status_unk;
    const char *status_full;
    const char *status_idle;
    int low_threshold;
    char *threshold_type;
    bool last_full_capacity;
    const char *format_percentage;
    bool hide_seconds;
} battery_info_ctx_t;

void print_battery_info(battery_info_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *title;
    const char *format;
    const char *tz;
    const char *locale;
    const char *format_time;
    bool hide_if_equals_localtime;
    time_t t;
} time_ctx_t;

void print_time(time_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *format;
    time_t t;
} ddate_ctx_t;

const char *get_ip_addr(const char *interface, int family);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *interface;
    const char *format_up;
    const char *format_down;
    const char *format_bitrate;
    const char *format_noise;
    const char *format_quality;
    const char *format_signal;
} wireless_info_ctx_t;

void print_wireless_info(wireless_info_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    int zone;
    const char *path;
    const char *format;
    const char *format_above_threshold;
    int max_threshold;
} cpu_temperature_ctx_t;

void print_cpu_temperature_info(cpu_temperature_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *format;
    const char *format_above_threshold;
    const char *format_above_degraded_threshold;
    const char *path;
    const float max_threshold;
    const float degraded_threshold;
} cpu_usage_ctx_t;

void print_cpu_usage(cpu_usage_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *interface;
    const char *format_up;
    const char *format_down;
} eth_info_ctx_t;

void print_eth_info(eth_info_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *format;
    const char *format_above_threshold;
    const float max_threshold;
} load_ctx_t;

void print_load(load_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *format;
    const char *format_degraded;
    const char *threshold_degraded;
    const char *threshold_critical;
    const char *memory_used_method;
    const char *unit;
    const int decimals;
} memory_ctx_t;

void print_memory(memory_ctx_t *ctx);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *fmt;
    const char *fmt_muted;
    const char *device;
    const char *mixer;
    int mixer_idx;
} volume_ctx_t;

void print_volume(volume_ctx_t *ctx);

bool process_runs(const char *path);
int volume_pulseaudio(uint32_t sink_idx, const char *sink_name);
bool description_pulseaudio(uint32_t sink_idx, const char *sink_name, char buffer[MAX_SINK_DESCRIPTION_LEN]);
bool pulse_initialize(void);

typedef struct {
    yajl_gen json_gen;
    char *buf;
    const size_t buflen;
    const char *title;
    const char *path;
    const char *format;
    const char *format_bad;
    const int max_chars;
} file_contents_ctx_t;

/* socket file descriptor for general purposes */
extern int general_socket;

extern cfg_t *cfg, *cfg_general, *cfg_section;

extern pthread_t main_thread;
#endif
