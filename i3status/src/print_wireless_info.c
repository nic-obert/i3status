#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#define IW_ESSID_MAX_SIZE 32

#include "i3status.h"

#define STRING_SIZE 60

#define WIRELESS_INFO_FLAG_HAS_ESSID (1 << 0)
#define WIRELESS_INFO_FLAG_HAS_QUALITY (1 << 1)
#define WIRELESS_INFO_FLAG_HAS_SIGNAL (1 << 2)
#define WIRELESS_INFO_FLAG_HAS_NOISE (1 << 3)
#define WIRELESS_INFO_FLAG_HAS_FREQUENCY (1 << 4)

#define PERCENT_VALUE(value, total) ((int)(value * 100 / (float)total + 0.5f))

typedef struct {
    long int bitrate;
    double frequency;
    char essid[IW_ESSID_MAX_SIZE + 1];
    uint8_t bssid[ETH_ALEN];
    int flags;
    int quality;
    int quality_max;
    int quality_average;
    int signal_level;
    int signal_level_max;
    int noise_level;
    int noise_level_max;
} wireless_info_t;

typedef struct {
    struct nl_sock *socket;
    int nl80211_id;
} wireless_nl_cache_t;

static wireless_nl_cache_t nl_cache;

static void invalidate_nl_cache(void) {
    // Safety: `nl_socket_free(NULL)` is expected to be safe in libnl
    nl_socket_free(nl_cache.socket);
    nl_cache.socket = NULL;
    nl_cache.nl80211_id = -1;
}

void free_wireless_info_cache(void) {
    invalidate_nl_cache();
}

static bool initialize_nl_cache(void) {
    if (nl_cache.socket != NULL)
        return true;

    nl_cache.socket = nl_socket_alloc();
    if (nl_cache.socket == NULL)
        return false;

    if (genl_connect(nl_cache.socket) != 0) {
        invalidate_nl_cache();
        return false;
    }

    nl_cache.nl80211_id = genl_ctrl_resolve(nl_cache.socket, "nl80211");
    if (nl_cache.nl80211_id < 0) {
        invalidate_nl_cache();
        return false;
    }

    return true;
}

// Like iw_print_bitrate, but without the dependency on libiw.
static void print_bitrate(char *buffer, int buflen, long int bitrate, const char *format_bitrate) {
    const int kilo = 1e3;
    const int mega = 1e6;
    const int giga = 1e9;

    const double rate = bitrate;
    char scale;
    int divisor;

    if (rate >= giga) {
        scale = 'G';
        divisor = giga;
    } else if (rate >= mega) {
        scale = 'M';
        divisor = mega;
    } else {
        scale = 'k';
        divisor = kilo;
    }
    snprintf(buffer, buflen, format_bitrate, rate / divisor, scale);
}

// Based on NetworkManager/src/platform/wifi/wifi-utils-nl80211.c
static uint32_t nl80211_xbm_to_percent(int32_t xbm, int32_t divisor) {
#define NOISE_FLOOR_DBM -90
#define SIGNAL_MAX_DBM -20

    xbm /= divisor;
    if (xbm < NOISE_FLOOR_DBM)
        xbm = NOISE_FLOOR_DBM;
    if (xbm > SIGNAL_MAX_DBM)
        xbm = SIGNAL_MAX_DBM;

    return 100 - 70 * (((float)SIGNAL_MAX_DBM - (float)xbm) / ((float)SIGNAL_MAX_DBM - (float)NOISE_FLOOR_DBM));
}

// Based on NetworkManager/src/platform/wifi/wifi-utils-nl80211.c
static void find_ssid(uint8_t *ies, uint32_t ies_len, uint8_t **ssid, uint32_t *ssid_len) {
#define WLAN_EID_SSID 0
    *ssid = NULL;
    *ssid_len = 0;

    while (ies_len > 2 && ies[0] != WLAN_EID_SSID) {
        ies_len -= ies[1] + 2;
        ies += ies[1] + 2;
    }
    if (ies_len < 2)
        return;
    if (ies_len < (uint32_t)(2 + ies[1]))
        return;

    *ssid_len = ies[1];
    *ssid = ies + 2;
}

static int gwi_sta_cb(struct nl_msg *msg, void *data) {
    wireless_info_t *info = data;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
        [NL80211_STA_INFO_RX_BITRATE] = {.type = NLA_NESTED},
    };

    static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
        [NL80211_RATE_INFO_BITRATE] = {.type = NLA_U16},
    };

    if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) < 0)
        return NL_SKIP;

    if (tb[NL80211_ATTR_STA_INFO] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], stats_policy))
        return NL_SKIP;

    if (sinfo[NL80211_STA_INFO_RX_BITRATE] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_RX_BITRATE], rate_policy))
        return NL_SKIP;

    if (rinfo[NL80211_RATE_INFO_BITRATE] == NULL)
        return NL_SKIP;

    // NL80211_RATE_INFO_BITRATE is specified in units of 100 kbit/s, but iw
    // used to specify bit/s, so we convert to use the same code path.
    info->bitrate = (long int)nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]) * 100 * 1000;

    if (sinfo[NL80211_STA_INFO_SIGNAL] != NULL) {
        info->flags |= WIRELESS_INFO_FLAG_HAS_SIGNAL;
        info->signal_level = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);

        info->flags |= WIRELESS_INFO_FLAG_HAS_QUALITY;
        info->quality = nl80211_xbm_to_percent(info->signal_level, 1);
        info->quality_max = 100;
        info->quality_average = 50;
    }

    return NL_SKIP;
}

static int gwi_scan_cb(struct nl_msg *msg, void *data) {
    wireless_info_t *info = data;
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *bss[NL80211_BSS_MAX + 1];
    struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
        [NL80211_BSS_FREQUENCY] = {.type = NLA_U32},
        [NL80211_BSS_BSSID] = {.type = NLA_UNSPEC},
        [NL80211_BSS_INFORMATION_ELEMENTS] = {.type = NLA_UNSPEC},
        [NL80211_BSS_SIGNAL_MBM] = {.type = NLA_U32},
        [NL80211_BSS_SIGNAL_UNSPEC] = {.type = NLA_U8},
        [NL80211_BSS_STATUS] = {.type = NLA_U32},
    };

    if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) < 0)
        return NL_SKIP;

    if (tb[NL80211_ATTR_BSS] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(bss, NL80211_BSS_MAX, tb[NL80211_ATTR_BSS], bss_policy))
        return NL_SKIP;

    if (bss[NL80211_BSS_STATUS] == NULL)
        return NL_SKIP;

    const uint32_t status = nla_get_u32(bss[NL80211_BSS_STATUS]);

    if (status != NL80211_BSS_STATUS_ASSOCIATED &&
        status != NL80211_BSS_STATUS_IBSS_JOINED)
        return NL_SKIP;

    if (bss[NL80211_BSS_BSSID] == NULL)
        return NL_SKIP;

    memcpy(info->bssid, nla_data(bss[NL80211_BSS_BSSID]), ETH_ALEN);

    if (bss[NL80211_BSS_FREQUENCY]) {
        info->flags |= WIRELESS_INFO_FLAG_HAS_FREQUENCY;
        info->frequency = (double)nla_get_u32(bss[NL80211_BSS_FREQUENCY]) * 1e6;
    }

    if (bss[NL80211_BSS_SIGNAL_UNSPEC]) {
        info->flags |= WIRELESS_INFO_FLAG_HAS_SIGNAL;
        info->signal_level = nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]);
        info->signal_level_max = 100;

        info->flags |= WIRELESS_INFO_FLAG_HAS_QUALITY;
        info->quality = info->signal_level;
        info->quality_max = 100;
        info->quality_average = 50;
    }

    if (bss[NL80211_BSS_SIGNAL_MBM]) {
        info->flags |= WIRELESS_INFO_FLAG_HAS_SIGNAL;
        info->signal_level = (int)nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]) / 100;

        info->flags |= WIRELESS_INFO_FLAG_HAS_QUALITY;
        info->quality = nl80211_xbm_to_percent(nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]), 100);
        info->quality_max = 100;
        info->quality_average = 50;
    }

    if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
        uint8_t *ssid;
        uint32_t ssid_len;

        find_ssid(nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]),
                  nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]),
                  &ssid, &ssid_len);
        if (ssid && ssid_len) {
            info->flags |= WIRELESS_INFO_FLAG_HAS_ESSID;
            snprintf(info->essid, sizeof(info->essid), "%.*s", ssid_len, ssid);
        }
    }

    return NL_SKIP;
}

static int get_wireless_info(const char *interface, wireless_info_t *info) {
    memset(info, 0, sizeof(wireless_info_t));

    /*
     * Query the kernel's nl80211 generic-netlink interface. The scan request
     * identifies the currently associated BSSID and supplies the ESSID,
     * frequency, signal strength, and link-quality data. The station request
     * then uses that BSSID to obtain the receive bitrate.
     */
    if (!initialize_nl_cache())
        return 0;

    struct nl_sock *sk = nl_cache.socket;
    const int nl80211_id = nl_cache.nl80211_id;

    if (nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, gwi_scan_cb, info) < 0) {
        invalidate_nl_cache();
        return 0;
    }

    const unsigned int ifidx = if_nametoindex(interface);
    if (ifidx == 0)
        return 0;

    struct nl_msg *msg = NULL;

    /* Dump scan results; gwi_scan_cb keeps only the associated network. */
    if ((msg = nlmsg_alloc()) == NULL)
        return 0;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_SCAN, 0) ||
        nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx) < 0)
        goto error2;

    if (nl_send_sync(sk, msg) < 0) {
        // nl_send_sync calls nlmsg_free()
        invalidate_nl_cache();
        return 0;
    }
    msg = NULL;

    if (nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, gwi_sta_cb, info) < 0) {
        invalidate_nl_cache();
        return 0;
    }

    /* Query station statistics for the BSSID found in the scan response. */
    if ((msg = nlmsg_alloc()) == NULL)
        return 0;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0) || nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx) < 0 || nla_put(msg, NL80211_ATTR_MAC, 6, info->bssid) < 0)
        goto error2;

    if (nl_send_sync(sk, msg) < 0) {
        // nl_send_sync calls nlmsg_free()
        invalidate_nl_cache();
        return 0;
    }
    msg = NULL;

    return 1;

error2:
    /* msg is still owned by this function on message-construction errors. */
    nlmsg_free(msg);
    return 0;
}

/* Table summarizing what is the decision to prefer IPv4 or IPv6
 * based on their values.
 *
 * | ipv4_address | ipv6_address | Chosen IP | Color             |
 * |--------------|--------------|-----------|-------------------|
 * | NULL         | NULL         | None      | bad (red)         |
 * | NULL         | no IP        | IPv6      | degraded (orange) |
 * | NULL         | ::1/128      | IPv6      | ok (green)        |
 * | no IP        | NULL         | IPv4      | degraded          |
 * | no IP        | no IP        | IPv4      | degraded          |
 * | no IP        | ::1/128      | IPv6      | ok                |
 * | 127.0.0.1    | NULL         | IPv4      | ok                |
 * | 127.0.0.1    | no IP        | IPv4      | ok                |
 * | 127.0.0.1    | ::1/128      | IPv4      | ok                |
 */
void print_wireless_info(wireless_info_ctx_t *ctx) {

    char *outwalk = out_ctx.buf;
    const char *interface;

    if (ctx->use_first) {
        interface = it_cache.wireless_first;
    } else {
        interface = ctx->interface;
    }
    
    if (interface == NULL) {
        START_COLOR(bad);
        outwalk += sprintf(out_ctx.buf, "%s", ctx->format_down);
        OUTPUT_FULL_TEXT(out_ctx.buf);
        return;
    }

    const char *walk;
    wireless_info_t info;

    INSTANCE(interface);

    ip_addr_t addresses = get_ip_addr_ipv4_ipv6(interface);
    char *ipv4_address = addresses.ipv4_address;
    char *ipv6_address = addresses.ipv6_address;

    /*
     * Removing '%' and following characters from IPv6 since the interface identifier is redundant,
     * as the output already includes the interface name.
     */
    if (ipv6_address != NULL) {
        char *prct_ptr = strstr(ipv6_address, "%");
        if (prct_ptr != NULL) {
            *prct_ptr = '\0';
        }
    }

    bool prefer_ipv4 = true;
    if (ipv4_address == NULL) {
        if (ipv6_address == NULL) {
            START_COLOR(bad);
            outwalk += sprintf(outwalk, "%s", ctx->format_down);

            free(ipv4_address);
            free(ipv6_address);
            OUTPUT_FULL_TEXT(out_ctx.buf);
            return;
        } else {
            prefer_ipv4 = false;
        }
    } else if (BEGINS_WITH(ipv4_address, "no IP") && ipv6_address != NULL && !BEGINS_WITH(ipv6_address, "no IP")) {
        prefer_ipv4 = false;
    }

    const char *ip_address = (prefer_ipv4) ? ipv4_address : ipv6_address;
    if (!get_wireless_info(interface, &info)) {
        walk = ctx->format_down;
        START_COLOR(bad);
    } else {
        walk = ctx->format_up;
        if (info.flags & WIRELESS_INFO_FLAG_HAS_QUALITY) {
            const char* color = info.quality < info.quality_average ? general_cfg->color_opts.degraded : general_cfg->color_opts.good;
            START_COLOR_VAR(color);
        }
        else {
            if (BEGINS_WITH(ip_address, "no IP")) {
                START_COLOR(degraded);
            } else {
                START_COLOR(good);
            }
        }
    }

    char string_quality[STRING_SIZE] = {'\0'};
    char string_signal[STRING_SIZE] = {'\0'};
    char string_noise[STRING_SIZE] = {'\0'};
    char string_essid[STRING_SIZE] = {'\0'};
    char string_frequency[STRING_SIZE] = {'\0'};
    char string_ip[STRING_SIZE] = {'\0'};
    char string_bitrate[STRING_SIZE] = {'\0'};

    if (info.flags & WIRELESS_INFO_FLAG_HAS_QUALITY) {
        if (info.quality_max)
            snprintf(string_quality, STRING_SIZE, ctx->format_quality, PERCENT_VALUE(info.quality, info.quality_max), "%");
        else
            snprintf(string_quality, STRING_SIZE, "%d", info.quality);
    } else {
        snprintf(string_quality, STRING_SIZE, "?");
    }

    if (info.flags & WIRELESS_INFO_FLAG_HAS_SIGNAL) {
        if (info.signal_level_max)
            snprintf(string_signal, STRING_SIZE, ctx->format_signal, PERCENT_VALUE(info.signal_level, info.signal_level_max), "%");
        else
            snprintf(string_signal, STRING_SIZE, "%d dBm", info.signal_level);
    } else {
        snprintf(string_signal, STRING_SIZE, "?");
    }

    if (info.flags & WIRELESS_INFO_FLAG_HAS_NOISE) {
        if (info.noise_level_max)
            snprintf(string_noise, STRING_SIZE, ctx->format_noise, PERCENT_VALUE(info.noise_level, info.noise_level_max), "%");
        else
            snprintf(string_noise, STRING_SIZE, "%d dBm", info.noise_level);
    } else {
        snprintf(string_noise, STRING_SIZE, "?");
    }

    if (info.flags & WIRELESS_INFO_FLAG_HAS_ESSID)
        snprintf(string_essid, STRING_SIZE, "%s", info.essid);
    else
        snprintf(string_essid, STRING_SIZE, "?");

    if (info.flags & WIRELESS_INFO_FLAG_HAS_FREQUENCY)
        snprintf(string_frequency, STRING_SIZE, "%1.1f GHz", info.frequency / 1e9);
    else
        snprintf(string_frequency, STRING_SIZE, "?");

    snprintf(string_ip, STRING_SIZE, "%s", ip_address);

    print_bitrate(string_bitrate, sizeof(string_bitrate), info.bitrate, ctx->format_bitrate);

    placeholder_t placeholders[] = {
        {.name = "%quality", .value = string_quality},
        {.name = "%signal", .value = string_signal},
        {.name = "%noise", .value = string_noise},
        {.name = "%essid", .value = string_essid},
        {.name = "%frequency", .value = string_frequency},
        {.name = "%ip", .value = string_ip},
        {.name = "%bitrate", .value = string_bitrate}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(walk, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);

    free(ipv4_address);
    free(ipv6_address);
    OUTPUT_FULL_TEXT(out_ctx.buf);
}
