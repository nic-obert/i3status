#include <sys/socket.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <stdio.h>

#include "i3status.h"

#define LOOPBACK_DEV "lo"

static bool sysfs_devtype(char *dest, size_t n, const char *ifnam) {
    FILE *fp;
    char buf[1024];

    snprintf(buf, sizeof(buf), "/sys/class/net/%s/uevent", ifnam);
    if ((fp = fopen(buf, "r")) == NULL)
        return false;

    dest[0] = '\0';

    while (fgets(buf, sizeof(buf), fp)) {
        size_t slen;
        char *s;

        slen = strlen(buf);
        /* Line is too long to fit in the buffer */
        if (buf[slen - 1] != '\n' && !feof(fp))
            break;
        if ((s = strchr(buf, '='))) {
            if (strncmp(buf, "DEVTYPE", s - buf))
                continue;
            buf[slen - 1] = '\0';
            strncpy(dest, ++s, n);
            break;
        }
    }
    fclose(fp);
    return true;
}

static bool is_virtual(const char *ifname) {
    char path[1024];
    char *target = NULL;
    bool is_virtual = false;

    snprintf(path, sizeof(path), "/sys/class/net/%s", ifname);
    if ((target = realpath(path, NULL))) {
        if (BEGINS_WITH(target, "/sys/devices/virtual/")) {
            is_virtual = true;
        }
        free(target);
    }

    return is_virtual;
}

static net_type_t iface_type(const char *ifname) {
    char devtype[32];

    if (!sysfs_devtype(devtype, sizeof(devtype), ifname))
        return NET_TYPE_OTHER;

    /* Default to Ethernet when no devtype is available */
    if (!devtype[0])
        return NET_TYPE_ETHERNET;

    if (strcmp(devtype, "wlan") == 0)
        return NET_TYPE_WIRELESS;

    if (strcmp(devtype, "wwan") == 0)
        return NET_TYPE_OTHER;

    return NET_TYPE_OTHER;
}


/*
    Return the first network interface of the requested type among the given list of interfaces.    
*/
const char *first_net_interface(const net_type_t type, const struct ifaddrs *ifaddr) {
    struct ifaddrs *addrp;
    net_type_t iftype;

    for (addrp = ifaddr; addrp != NULL; addrp = addrp->ifa_next) {
        if (strncasecmp(LOOPBACK_DEV, addrp->ifa_name, strlen(LOOPBACK_DEV)) == 0)
            continue;
        if (addrp->ifa_addr == NULL)
            continue;
        // Skip AF_PACKET addresses (MAC addresses), as they are present on any interface.
        if (addrp->ifa_addr->sa_family != AF_INET &&
            addrp->ifa_addr->sa_family != AF_INET6)
            continue;
        // Skip this interface if it's not the requested type.
        iftype = iface_type(addrp->ifa_name);
        if (iftype != type)
            continue;
        if (is_virtual(addrp->ifa_name))
            continue;

        return addrp->ifa_name;
    }

    return NULL;
}
