#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_now;
static void on_alarm(int sig) { (void)sig; stop_now = 1; }

int main(int argc, char **argv) {
    const char *iface = argc > 1 ? argv[1] : "eth0";
    int seconds = argc > 2 ? atoi(argv[2]) : 15;
    const uint8_t radar_mac[6] = {0x78, 0x22, 0xf6, 0x33, 0xc7, 0x77};
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_ll bind_addr = {0};
    bind_addr.sll_family = AF_PACKET;
    bind_addr.sll_protocol = htons(ETH_P_ALL);
    bind_addr.sll_ifindex = if_nametoindex(iface);
    if (!bind_addr.sll_ifindex || bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind"); close(fd); return 1;
    }
    signal(SIGALRM, on_alarm);
    alarm((unsigned)seconds);
    uint64_t all = 0, radar = 0, radar_udp = 0, radar_arp = 0;
    uint8_t buf[65536];
    while (!stop_now) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) { if (errno == EINTR) continue; perror("recv"); break; }
        if (n < (ssize_t)sizeof(struct ethhdr)) continue;
        ++all;
        struct ethhdr *eth = (struct ethhdr *)buf;
        if (memcmp(eth->h_source, radar_mac, 6) != 0) continue;
        ++radar;
        uint16_t proto = ntohs(eth->h_proto);
        if (proto == ETH_P_ARP) { ++radar_arp; continue; }
        if (proto != ETH_P_IP || n < (ssize_t)(sizeof(*eth) + sizeof(struct iphdr))) continue;
        struct iphdr *ip = (struct iphdr *)(buf + sizeof(*eth));
        char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip->saddr, src, sizeof(src));
        inet_ntop(AF_INET, &ip->daddr, dst, sizeof(dst));
        if (ip->protocol == IPPROTO_UDP && n >= (ssize_t)(sizeof(*eth) + ip->ihl * 4 + sizeof(struct udphdr))) {
            struct udphdr *udp = (struct udphdr *)((uint8_t *)ip + ip->ihl * 4);
            ++radar_udp;
            if (radar_udp <= 20)
                printf("RADAR_UDP src=%s:%u dst=%s:%u bytes=%u\n", src, ntohs(udp->source),
                       dst, ntohs(udp->dest), ntohs(udp->len));
        } else if (radar <= 20) {
            printf("RADAR_IP proto=%u src=%s dst=%s bytes=%zd\n", ip->protocol, src, dst, n);
        }
        fflush(stdout);
    }
    printf("SUMMARY iface=%s seconds=%d all=%llu radar=%llu radar_udp=%llu radar_arp=%llu\n",
           iface, seconds, (unsigned long long)all, (unsigned long long)radar,
           (unsigned long long)radar_udp, (unsigned long long)radar_arp);
    close(fd);
    return 0;
}
