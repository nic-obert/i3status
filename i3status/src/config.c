#include <config.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yajl/yajl_parse.h>

#include "i3status.h"
#include "config_defaults.h"

typedef enum {
    MAP_ROOT,
    MAP_GENERAL,
    MAP_ORDER,
    MAP_ITEM
} map_type_t;

typedef struct {
    config_t *config;
    map_type_t map;
    map_type_t map_stack[8];
    status_item_t *item;
    char *key;
    unsigned int depth;
} parser_state_t;

static bool valid_color(const char *value) {
    if (value == NULL || strlen(value) != 7 || value[0] != '#')
        return false;
    for (int i = 1; i < 7; i++) {
        if ((value[i] < '0' || value[i] > '9') &&
            (value[i] < 'a' || value[i] > 'f') &&
            (value[i] < 'A' || value[i] > 'F'))
            return false;
    }
    return true;
}

static char *copy_json_string(const unsigned char *value, size_t length) {
    char *result = malloc(length + 1);
    if (result == NULL)
        return NULL;
    memcpy(result, value, length);
    result[length] = '\0';
    return result;
}

static bool set_string(const unsigned char *value, size_t length, const char **target) {
    char *copy = copy_json_string(value, length);
    if (copy == NULL)
        return false;
    free((char *)*target);
    *target = copy;
    return true;
}

static bool parse_integer(const char *value, int *result) {
    char *end;
    long parsed = strtol(value, &end, 10);
    if (*value == '\0' || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
        return false;
    *result = (int)parsed;
    return true;
}

static bool parse_unsigned(const char *value, unsigned int *result) {
    char *end;
    unsigned long parsed = strtoul(value, &end, 10);
    if (*value == '\0' || *end != '\0' || parsed > UINT_MAX)
        return false;
    *result = (unsigned int)parsed;
    return true;
}

static bool parse_float(const char *value, float *result) {
    char *end;
    float parsed = strtof(value, &end);
    if (*value == '\0' || *end != '\0')
        return false;
    *result = parsed;
    return true;
}

static bool set_module_type(status_item_t *item, const char *name) {
    static const struct {
        const char *name;
        item_type_t type;
    } types[] = {
        {"ipv6", ITEM_IPV6},
        {"wireless", ITEM_WIRELESS},
        {"ethernet", ITEM_ETHERNET},
        {"battery", ITEM_BATTERY},
        {"load", ITEM_LOAD},
        {"memory", ITEM_MEMORY},
        {"tztime", ITEM_TZTIME},
        {"volume", ITEM_VOLUME},
        {"cpu_temperature", ITEM_CPU_TEMPERATURE},
        {"cpu_usage", ITEM_CPU_USAGE},
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(name, types[i].name) == 0) {
            item->type = types[i].type;
            return true;
        }
    }
    return false;
}

static void init_item(status_item_t *item, const char *name) {
    memset(item, 0, sizeof(*item));
    set_module_type(item, name);
    switch (item->type) {
    case ITEM_IPV6:
        item->ctx.ipv6.format_up = sstrdup(CONFIG_DEFAULT_IPV6_FORMAT_UP);
        item->ctx.ipv6.format_down = sstrdup(CONFIG_DEFAULT_IPV6_FORMAT_DOWN);
        break;
    case ITEM_WIRELESS:
        item->ctx.wireless.format_up = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_UP);
        item->ctx.wireless.format_down = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_DOWN);
        item->ctx.wireless.format_bitrate = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_BITRATE);
        item->ctx.wireless.format_noise = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_NOISE);
        item->ctx.wireless.format_quality = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_QUALITY);
        item->ctx.wireless.format_signal = sstrdup(CONFIG_DEFAULT_WIRELESS_FORMAT_SIGNAL);
        break;
    case ITEM_ETHERNET:
        item->ctx.ethernet.format_up = sstrdup(CONFIG_DEFAULT_ETHERNET_FORMAT_UP);
        item->ctx.ethernet.format_down = sstrdup(CONFIG_DEFAULT_ETHERNET_FORMAT_DOWN);
        break;
    case ITEM_BATTERY:
        item->ctx.battery.path = sstrdup(CONFIG_DEFAULT_BATTERY_PATH);
        item->ctx.battery.format = sstrdup(CONFIG_DEFAULT_BATTERY_FORMAT);
        item->ctx.battery.format_down = sstrdup(CONFIG_DEFAULT_BATTERY_FORMAT_DOWN);
        item->ctx.battery.format_percentage = sstrdup(CONFIG_DEFAULT_BATTERY_FORMAT_PERCENTAGE);
        item->ctx.battery.status_chr = sstrdup(CONFIG_DEFAULT_BATTERY_STATUS_CHR);
        item->ctx.battery.status_bat = sstrdup(CONFIG_DEFAULT_BATTERY_STATUS_BAT);
        item->ctx.battery.status_unk = sstrdup(CONFIG_DEFAULT_BATTERY_STATUS_UNK);
        item->ctx.battery.status_full = sstrdup(CONFIG_DEFAULT_BATTERY_STATUS_FULL);
        item->ctx.battery.status_idle = sstrdup(CONFIG_DEFAULT_BATTERY_STATUS_IDLE);
        item->ctx.battery.number = CONFIG_DEFAULT_BATTERY_NUMBER;
        item->ctx.battery.threshold_type = CONFIG_DEFAULT_BATTERY_THRESHOLD_TYPE;
        item->ctx.battery.low_threshold = CONFIG_DEFAULT_BATTERY_LOW_THRESHOLD;
        item->ctx.battery.hide_seconds = CONFIG_DEFAULT_BATTERY_HIDE_SECONDS;
        break;
    case ITEM_LOAD:
        item->ctx.load.format = sstrdup(CONFIG_DEFAULT_LOAD_FORMAT);
        item->ctx.load.max_threshold = CONFIG_DEFAULT_LOAD_MAX_THRESHOLD;
        break;
    case ITEM_MEMORY:
        item->ctx.memory.format = sstrdup(CONFIG_DEFAULT_MEMORY_FORMAT);
        item->ctx.memory.memory_used_method = sstrdup(CONFIG_DEFAULT_MEMORY_USED_METHOD);
        item->ctx.memory.unit = sstrdup(CONFIG_DEFAULT_MEMORY_UNIT);
        item->ctx.memory.decimals = CONFIG_DEFAULT_MEMORY_DECIMALS;
        break;
    case ITEM_TZTIME:
        item->ctx.tztime.format = sstrdup(CONFIG_DEFAULT_TZTIME_FORMAT);
        item->ctx.tztime.tz = sstrdup(CONFIG_DEFAULT_TZTIME_TZ);
        item->ctx.tztime.locale = sstrdup(CONFIG_DEFAULT_TZTIME_LOCALE);
        break;
    case ITEM_VOLUME:
        item->ctx.volume.fmt = sstrdup(CONFIG_DEFAULT_VOLUME_FORMAT);
        item->ctx.volume.fmt_muted = sstrdup(CONFIG_DEFAULT_VOLUME_FORMAT_MUTED);
        item->ctx.volume.device = sstrdup(CONFIG_DEFAULT_VOLUME_DEVICE);
        item->ctx.volume.mixer = sstrdup(CONFIG_DEFAULT_VOLUME_MIXER);
        break;
    case ITEM_CPU_TEMPERATURE:
        item->ctx.cpu_temperature.format = sstrdup(CONFIG_DEFAULT_CPU_TEMPERATURE_FORMAT);
        item->ctx.cpu_temperature.max_threshold = CONFIG_DEFAULT_CPU_TEMPERATURE_MAX_THRESHOLD;
        break;
    case ITEM_CPU_USAGE:
        item->ctx.cpu_usage.format = sstrdup(CONFIG_DEFAULT_CPU_USAGE_FORMAT);
        item->ctx.cpu_usage.path = sstrdup(CONFIG_DEFAULT_CPU_USAGE_PATH);
        item->ctx.cpu_usage.max_threshold = CONFIG_DEFAULT_CPU_USAGE_MAX_THRESHOLD;
        item->ctx.cpu_usage.degraded_threshold = CONFIG_DEFAULT_CPU_USAGE_DEGRADED_THRESHOLD;
        break;
    }
}

static status_item_t *append_item(parser_state_t *state, const char *name) {
    status_item_t *items = realloc(state->config->items, (state->config->item_count + 1) * sizeof(*items));
    if (items == NULL)
        return NULL;
    if (!set_module_type(&items[state->config->item_count], name))
        return NULL;
    state->config->items = items;
    state->item = &items[state->config->item_count++];
    init_item(state->item, name);
    return state->item;
}

static bool set_general_string(parser_state_t *state, const char *key, const unsigned char *value, size_t length) {
    general_opts_t *general = &state->config->general;
    if (strcmp(key, "separator") == 0)
        return set_string(value, length, &general->separator);
    char *target = NULL;
    if (strcmp(key, "color_separator") == 0) target = general->color_separator;
    else if (strcmp(key, "color_good") == 0) target = general->color_opts.good;
    else if (strcmp(key, "color_degraded") == 0) target = general->color_opts.degraded;
    else if (strcmp(key, "color_bad") == 0) target = general->color_opts.bad;
    if (target != NULL && length < 8) {
        memcpy(target, value, length);
        target[length] = '\0';
        return true;
    }
    return false;
}

static bool set_item_string(status_item_t *item, const char *key, const unsigned char *value, size_t length) {
#define STR(name, field) if (strcmp(key, name) == 0) return set_string(value, length, &field)
    switch (item->type) {
    case ITEM_IPV6: STR("format_up", item->ctx.ipv6.format_up); STR("format_down", item->ctx.ipv6.format_down); break;
    case ITEM_WIRELESS:
        STR("interface", item->ctx.wireless.interface); STR("format_up", item->ctx.wireless.format_up); STR("format_down", item->ctx.wireless.format_down);
        STR("format_bitrate", item->ctx.wireless.format_bitrate); STR("format_noise", item->ctx.wireless.format_noise);
        STR("format_quality", item->ctx.wireless.format_quality); STR("format_signal", item->ctx.wireless.format_signal); break;
    case ITEM_ETHERNET: STR("interface", item->ctx.ethernet.interface); STR("format_up", item->ctx.ethernet.format_up); STR("format_down", item->ctx.ethernet.format_down); break;
    case ITEM_BATTERY:
        STR("path", item->ctx.battery.path); STR("format", item->ctx.battery.format); STR("format_down", item->ctx.battery.format_down);
        STR("status_chr", item->ctx.battery.status_chr); STR("status_bat", item->ctx.battery.status_bat); STR("status_unk", item->ctx.battery.status_unk);
        STR("status_full", item->ctx.battery.status_full); STR("status_idle", item->ctx.battery.status_idle); STR("format_percentage", item->ctx.battery.format_percentage); break;
    case ITEM_LOAD: STR("format", item->ctx.load.format); STR("format_above_threshold", item->ctx.load.format_above_threshold); break;
    case ITEM_MEMORY:
        STR("format", item->ctx.memory.format); STR("format_degraded", item->ctx.memory.format_degraded); STR("threshold_degraded", item->ctx.memory.threshold_degraded);
        STR("threshold_critical", item->ctx.memory.threshold_critical); STR("memory_used_method", item->ctx.memory.memory_used_method); STR("unit", item->ctx.memory.unit); break;
    case ITEM_TZTIME:
        if (strcmp(key, "title") == 0) return set_string(value, length, &item->ctx.tztime.title);
        if (strcmp(key, "format") == 0) return set_string(value, length, &item->ctx.tztime.format);
        if (strcmp(key, "tz") == 0 || strcmp(key, "timezone") == 0) return set_string(value, length, &item->ctx.tztime.tz);
        STR("locale", item->ctx.tztime.locale); STR("format_time", item->ctx.tztime.format_time); break;
    case ITEM_VOLUME:
        if (strcmp(key, "title") == 0) return true;
        if (strcmp(key, "format") == 0) return set_string(value, length, &item->ctx.volume.fmt);
        if (strcmp(key, "format_muted") == 0) return set_string(value, length, &item->ctx.volume.fmt_muted);
        STR("device", item->ctx.volume.device); STR("mixer", item->ctx.volume.mixer); break;
    case ITEM_CPU_TEMPERATURE: STR("path", item->ctx.cpu_temperature.path); STR("format", item->ctx.cpu_temperature.format); STR("format_above_threshold", item->ctx.cpu_temperature.format_above_threshold); break;
    case ITEM_CPU_USAGE: STR("format", item->ctx.cpu_usage.format); STR("format_above_threshold", item->ctx.cpu_usage.format_above_threshold); STR("format_above_degraded_threshold", item->ctx.cpu_usage.format_above_degraded_threshold); STR("path", item->ctx.cpu_usage.path); break;
    }
#undef STR
    return false;
}

static bool set_item_number(status_item_t *item, const char *key, const char *value) {
    int integer;
    float real;
    if (item->type == ITEM_BATTERY && strcmp(key, "low_threshold") == 0) {
        if (!parse_integer(value, &integer) || integer < 0 || integer > UINT8_MAX) return false;
        item->ctx.battery.low_threshold = (uint8_t)integer; return true;
    }
    if (item->type == ITEM_BATTERY && strcmp(key, "mixer_idx") == 0) return false;
    if (item->type == ITEM_BATTERY && strcmp(key, "batno") == 0)
        return parse_integer(value, &item->ctx.battery.number);
    if (item->type == ITEM_MEMORY && strcmp(key, "decimals") == 0)
        return parse_integer(value, &item->ctx.memory.decimals);
    if (item->type == ITEM_VOLUME && strcmp(key, "mixer_idx") == 0)
        return parse_integer(value, &item->ctx.volume.mixer_idx);
    if (item->type == ITEM_CPU_TEMPERATURE && strcmp(key, "zone") == 0)
        return parse_integer(value, &item->ctx.cpu_temperature.zone);
    if (item->type == ITEM_CPU_TEMPERATURE && strcmp(key, "max_threshold") == 0) {
        if (!parse_integer(value, &item->ctx.cpu_temperature.max_threshold)) return false;
        return true;
    }
    if (item->type == ITEM_LOAD && strcmp(key, "max_threshold") == 0) return parse_float(value, &item->ctx.load.max_threshold);
    if (item->type == ITEM_CPU_USAGE && strcmp(key, "max_threshold") == 0) return parse_float(value, &item->ctx.cpu_usage.max_threshold);
    if (item->type == ITEM_CPU_USAGE && strcmp(key, "degraded_threshold") == 0) return parse_float(value, &item->ctx.cpu_usage.degraded_threshold);
    (void)real;
    return false;
}

static bool set_item_boolean(status_item_t *item, const char *key, int value) {
    if (item->type == ITEM_WIRELESS && strcmp(key, "use_first") == 0) { item->ctx.wireless.use_first = value; return true; }
    if (item->type == ITEM_ETHERNET && strcmp(key, "use_first") == 0) { item->ctx.ethernet.use_first = value; return true; }
    if (item->type == ITEM_BATTERY && strcmp(key, "last_full_capacity") == 0) { item->ctx.battery.last_full_capacity = value; return true; }
    if (item->type == ITEM_BATTERY && strcmp(key, "hide_seconds") == 0) { item->ctx.battery.hide_seconds = value; return true; }
    if (item->type == ITEM_TZTIME && strcmp(key, "hide_if_equals_localtime") == 0) { item->ctx.tztime.hide_if_equals_localtime = value; return true; }
    return false;
}

static int yajl_start_map(void *ctx) {
    parser_state_t *state = ctx;
    if (state->depth < sizeof(state->map_stack) / sizeof(state->map_stack[0]))
        state->map_stack[state->depth] = state->map;
    if (state->depth == 0) {
        state->map = MAP_ROOT;
    } else if (state->depth == 1 && state->map == MAP_ROOT && state->key) {
        if (strcmp(state->key, "general") == 0) state->map = MAP_GENERAL;
        else if (strcmp(state->key, "order") == 0) state->map = MAP_ORDER;
        else return 0;
    } else if (state->depth == 2 && state->map == MAP_ORDER && state->key) {
        if (append_item(state, state->key) == NULL) return 0;
        state->map = MAP_ITEM;
    } else {
        return 0;
    }
    state->depth++;
    free(state->key);
    state->key = NULL;
    return 1;
}

static int yajl_end_map(void *ctx) {
    parser_state_t *state = ctx;
    if (state->depth == 0) return 0;
    state->depth--;
    if (state->depth < sizeof(state->map_stack) / sizeof(state->map_stack[0]))
        state->map = state->map_stack[state->depth];
    if (state->depth == 0) state->map = MAP_ROOT;
    if (state->depth == 1) state->item = NULL;
    free(state->key);
    state->key = NULL;
    return 1;
}

static int yajl_map_key(void *ctx, const unsigned char *key, size_t length) {
    parser_state_t *state = ctx;
    if (state->key != NULL) return 0;
    state->key = copy_json_string(key, length);
    return state->key != NULL;
}

static int yajl_string(void *ctx, const unsigned char *value, size_t length) {
    parser_state_t *state = ctx;
    bool ok = false;
    if (state->key == NULL) return 0;
    if (state->map == MAP_GENERAL) ok = set_general_string(state, state->key, value, length);
    else if (state->map == MAP_ITEM) {
        if (state->item->type == ITEM_BATTERY && strcmp(state->key, "threshold_type") == 0) {
            char *text = copy_json_string(value, length);
            ok = text && (strcmp(text, "time") == 0 || strcmp(text, "percentage") == 0);
            if (ok) state->item->ctx.battery.threshold_type = strcmp(text, "percentage") == 0 ? PERCENTAGE : TIME;
            free(text);
        } else if (state->item->type == ITEM_BATTERY && strcmp(state->key, "batno") == 0) {
            char *text = copy_json_string(value, length);
            ok = text && (strcmp(text, "all") == 0 || parse_integer(text, &state->item->ctx.battery.number));
            free(text);
        } else {
            ok = set_item_string(state->item, state->key, value, length);
            if (ok && state->item->type == ITEM_WIRELESS && strcmp(state->key, "interface") == 0 && length == 7 && memcmp(value, "_first_", 7) == 0)
                state->item->ctx.wireless.use_first = true;
            if (ok && state->item->type == ITEM_ETHERNET && strcmp(state->key, "interface") == 0 && length == 7 && memcmp(value, "_first_", 7) == 0)
                state->item->ctx.ethernet.use_first = true;
        }
    }
    free(state->key); state->key = NULL;
    return ok;
}

static int yajl_number(void *ctx, const char *value, size_t length) {
    parser_state_t *state = ctx;
    char *number = copy_json_string((const unsigned char *)value, length);
    bool ok = state->key != NULL && number != NULL;
    if (ok && state->map == MAP_GENERAL && strcmp(state->key, "interval") == 0) {
        unsigned int interval;
        ok = parse_unsigned(number, &interval) && interval > 0;
        if (ok)
            state->config->general.interval = interval;
    }
    else if (ok && state->map == MAP_ITEM)
        ok = set_item_number(state->item, state->key, number);
    free(number); free(state->key); state->key = NULL;
    return ok;
}

static int yajl_boolean(void *ctx, int value) {
    parser_state_t *state = ctx;
    bool ok = state->key != NULL;
    if (ok && state->map == MAP_GENERAL && strcmp(state->key, "colors") == 0) state->config->general.colors = value;
    else if (ok && state->map == MAP_ITEM) ok = set_item_boolean(state->item, state->key, value);
    else ok = false;
    free(state->key); state->key = NULL;
    return ok;
}

static int yajl_null(void *ctx) { (void)ctx; return 0; }
static int yajl_start_array(void *ctx) { (void)ctx; return 0; }
static int yajl_end_array(void *ctx) { (void)ctx; return 0; }

static const yajl_callbacks callbacks = {
    .yajl_null = yajl_null, .yajl_boolean = yajl_boolean, .yajl_number = yajl_number,
    .yajl_string = yajl_string, .yajl_start_map = yajl_start_map, .yajl_map_key = yajl_map_key,
    .yajl_end_map = yajl_end_map, .yajl_start_array = yajl_start_array, .yajl_end_array = yajl_end_array,
};

int config_parse(const char *filename, config_t *output) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "i3status: Cannot open JSON configuration '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    memset(output, 0, sizeof(*output));
    output->general.separator = sstrdup(CONFIG_DEFAULT_SEPARATOR);
    strcpy(output->general.color_separator, CONFIG_DEFAULT_COLOR_SEPARATOR);
    strcpy(output->general.color_opts.good, CONFIG_DEFAULT_COLOR_GOOD);
    strcpy(output->general.color_opts.degraded, CONFIG_DEFAULT_COLOR_DEGRADED);
    strcpy(output->general.color_opts.bad, CONFIG_DEFAULT_COLOR_BAD);
    output->general.interval = CONFIG_DEFAULT_INTERVAL;
    output->general.colors = CONFIG_DEFAULT_COLORS;

    parser_state_t state = {.config = output};
    yajl_handle handle = yajl_alloc(&callbacks, NULL, &state);
    if (handle == NULL) die("i3status: Cannot allocate yajl parser handle");
    yajl_config(handle, yajl_allow_comments, 1);
    unsigned char buffer[4096];
    size_t length;
    bool ok = true;
    while (ok && (length = fread(buffer, 1, sizeof(buffer), file)) > 0)
        ok = yajl_parse(handle, buffer, length) == yajl_status_ok;
    bool valid_json = ok && !ferror(file) && yajl_complete_parse(handle) == yajl_status_ok && state.depth == 0;
    ok = valid_json;
    if (ok && (!valid_color(output->general.color_opts.bad) ||
               !valid_color(output->general.color_opts.degraded) ||
               !valid_color(output->general.color_opts.good) ||
               !valid_color(output->general.color_separator))) {
        fprintf(stderr, "i3status: Bad color format\n");
        ok = false;
    }
    if (!valid_json)
        fprintf(stderr, "i3status: Invalid JSON configuration '%s'\n", filename);
    fclose(file);
    yajl_free(handle);
    free(state.key);
    return ok ? 0 : -1;
}
