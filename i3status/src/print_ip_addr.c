#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "i3status.h"


/*
    Match a string with an optional label suffix.
    Example:
    "eth0:label","eth0" -> true
    "eth0","eth0" -> true
    "wlan0","eth0" -> false 
*/
static bool interface_names_match(const char *ifa_name, const char *interface, size_t interface_len) {

    // Check if `ifa_name` begins with `interface`
    if (strncmp(ifa_name, interface, interface_len) != 0)
        return false;
    
    // If the first part of `ifa_name` matches, ensure there are no more characters
    char final_char = ifa_name[interface_len];
    return final_char == '\0' || final_char == ':';
}


/*
 * Return the IPv4 and IPv6 addresses for the given interface. An address is
 * NULL if the interface does not exist, or "no IP" if the interface is up
 * and running but has no address of that family.
 */
ip_addr_t get_ip_addr_ipv4_ipv6(const char *interface, const struct ifaddrs *ifaddr) {
    ip_addr_t result = {NULL, NULL};
    struct ifaddrs *addrp;
    bool found = false;
    bool down = false;
    size_t interface_len = strlen(interface);

    if (ifaddr == NULL)
        return result;

    for (addrp = ifaddr; addrp != NULL; addrp = addrp->ifa_next) {
        if (!interface_names_match(addrp->ifa_name, interface, interface_len)) {
            /* The interface does not have the right name, skip it. */
            continue;
        }

        found = true;
        down |= (addrp->ifa_flags & IFF_RUNNING) == 0;

        if (addrp->ifa_addr == NULL)
            continue;

        int family = addrp->ifa_addr->sa_family;
        char **address = NULL;
        socklen_t len = 0;
        if (family == AF_INET && result.ipv4_address == NULL) {
            address = &result.ipv4_address;
            len = sizeof(struct sockaddr_in);
        } else if (family == AF_INET6 && result.ipv6_address == NULL) {
            address = &result.ipv6_address;
            len = sizeof(struct sockaddr_in6);
        }

        if (address != NULL) {
            char part[512] = {0};
            int ret = getnameinfo(addrp->ifa_addr, len, part, sizeof(part), NULL, 0, NI_NUMERICHOST);
            if (ret != 0) {
                fprintf(stderr, "i3status: getnameinfo(): %s\n", gai_strerror(ret));
                *address = sstrdup("no IP");
            } else {
                *address = sstrdup(part);
            }
        }
    }

    if (found && !down) {
        if (result.ipv4_address == NULL)
            result.ipv4_address = sstrdup("no IP");
        if (result.ipv6_address == NULL)
            result.ipv6_address = sstrdup("no IP");
    }

    return result;
}
