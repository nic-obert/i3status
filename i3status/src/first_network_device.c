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
    char target[1024];
    ssize_t length; 

    snprintf(path, sizeof(path), "/sys/class/net/%s", ifname);
    
    length = readlink(path, target, sizeof(target)-1);
    if (length < 0)
        return false;

    target[length] = '\0';

    return strstr(target, "/devices/virtual/") != NULL;
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
    Cache the first network interface of each requested type.
    This function shall be called only after initializing the ifaddr cache.
    Ownership: the strings cached by this functions are owned by the ifaddr cache.
    This function cleans up the previously cached values.
*/
void cache_first_network_interfaces(bool requested_ethernet, bool requested_wireless) {
    const struct ifaddrs *addrp;
    net_type_t iftype;

    // Reinitialize cache
    it_cache.wireless_first = NULL;
    it_cache.ethernet_first = NULL;

    for (addrp = it_cache.ifaddr; addrp != NULL; addrp = addrp->ifa_next) {
        
        // Skip loopback devices
        if (strcmp(addrp->ifa_name, LOOPBACK_DEV) == 0)
            continue;

        // Skip if interface has no address
        if (addrp->ifa_addr == NULL)
            continue;

        // Skip AF_PACKET addresses (MAC addresses), as they are present on any interface.
        if (addrp->ifa_addr->sa_family != AF_INET &&
            addrp->ifa_addr->sa_family != AF_INET6)
            continue;

        // Skip virtual devices
        if (is_virtual(addrp->ifa_name))
            continue;

        iftype = iface_type(addrp->ifa_name);
        
        if (iftype == NET_TYPE_ETHERNET && requested_ethernet && it_cache.ethernet_first == NULL) {
            it_cache.ethernet_first = addrp->ifa_name; // TODO: consider whether we could cache the IP address of this interface
            if (requested_wireless && it_cache.wireless_first != NULL)
                return;
        }

        if (iftype == NET_TYPE_WIRELESS && requested_wireless && it_cache.wireless_first == NULL) {
            it_cache.wireless_first = addrp->ifa_name;
            if (requested_ethernet && it_cache.ethernet_first != NULL)
                return;
        }
        
    }
}
