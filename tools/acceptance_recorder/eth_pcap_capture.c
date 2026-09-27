/* Minimal AF_PACKET recorder for RK-side diagnostic evidence. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/filter.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct pcap_file_header {
    uint32_t magic, version_major_minor;
    int32_t thiszone;
    uint32_t sigfigs, snaplen, network;
};
struct pcap_packet_header { uint32_t ts_sec, ts_usec, incl_len, orig_len; };

static volatile sig_atomic_t keep_running = 1;
static void stop_capture(int unused) { (void)unused; keep_running = 0; }

static int has_tcp_port_7447(const unsigned char *buf, size_t len) {
    if (len < ETH_HLEN + sizeof(struct iphdr)) return 0;
    size_t off = ETH_HLEN;
    uint16_t ethertype;
    memcpy(&ethertype, buf + 12, sizeof ethertype);
    ethertype = ntohs(ethertype);
    while ((ethertype == ETH_P_8021Q || ethertype == ETH_P_8021AD) && len >= off + 4) {
        memcpy(&ethertype, buf + off + 2, sizeof ethertype);
        ethertype = ntohs(ethertype);
        off += 4;
    }
    if (ethertype != ETH_P_IP || len < off + sizeof(struct iphdr)) return 0;
    const struct iphdr *ip = (const struct iphdr *)(buf + off);
    if (ip->protocol != IPPROTO_TCP || ip->ihl < 5) return 0;
    off += (size_t)ip->ihl * 4;
    if (len < off + sizeof(struct tcphdr)) return 0;
    const struct tcphdr *tcp = (const struct tcphdr *)(buf + off);
    return ntohs(tcp->source) == 7447 || ntohs(tcp->dest) == 7447;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <interface> <output.pcap> <seconds>\n", argv[0]);
        return 2;
    }
    unsigned seconds = (unsigned)strtoul(argv[3], NULL, 10);
    int ifindex = if_nametoindex(argv[1]);
    if (!ifindex || !seconds) { fprintf(stderr, "invalid interface or duration\n"); return 2; }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("fopen"); return 1; }
    /* A large userspace buffer prevents one fwrite per packet from stalling the
       AF_PACKET receive loop during the 10-minute acceptance capture. */
    if (setvbuf(out, NULL, _IOFBF, 4U * 1024U * 1024U) != 0) {
        perror("setvbuf");
        return 1;
    }
    struct pcap_file_header global = {0xa1b2c3d4, 0x00040002, 0, 0, 65535, 1};
    if (fwrite(&global, sizeof global, 1, out) != 1) { perror("pcap header"); return 1; }
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) { perror("socket"); return 1; }
    int receive_bytes = 16 * 1024 * 1024;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &receive_bytes, sizeof receive_bytes) < 0
            && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_bytes, sizeof receive_bytes) < 0) {
        perror("SO_RCVBUF");
        return 1;
    }
    struct sockaddr_ll addr = {0}; addr.sll_family = AF_PACKET; addr.sll_ifindex = ifindex; addr.sll_protocol = htons(ETH_P_ALL);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    signal(SIGINT, stop_capture); signal(SIGTERM, stop_capture);
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC, &deadline); deadline.tv_sec += seconds;
    unsigned long kept = 0, seen = 0;
    unsigned char buf[65535];
    while (keep_running) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) break;
        ssize_t n = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(1000); continue; } perror("recv"); break; }
        ++seen;
        if (!has_tcp_port_7447(buf, (size_t)n)) continue;
        struct timeval tv; gettimeofday(&tv, NULL);
        struct pcap_packet_header packet = {(uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec, (uint32_t)n, (uint32_t)n};
        fwrite(&packet, sizeof packet, 1, out);
        fwrite(buf, (size_t)n, 1, out);
        ++kept;
    }
    struct tpacket_stats stats = {0};
    socklen_t stats_len = sizeof stats;
    int stats_ok = getsockopt(fd, SOL_PACKET, PACKET_STATISTICS, &stats, &stats_len) == 0;
    if (fflush(out) != 0 || ferror(out)) { perror("pcap write"); fclose(out); close(fd); return 1; }
    fclose(out); close(fd);
    fprintf(stderr, "pcap packets: kept=%lu seen=%lu kernel_packets=%u kernel_drops=%u\n",
            kept, seen, stats_ok ? stats.tp_packets : 0, stats_ok ? stats.tp_drops : 0);
    return stats_ok && stats.tp_drops ? 3 : 0;
}
