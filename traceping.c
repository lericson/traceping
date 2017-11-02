#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

static int error, sock, ttl;
static struct addrinfo hints, *res0, *dest;
struct {
    struct icmp icmp;
    char data[128];
} outmsg = {
    // A surprise to be sure, but a welcome one!
   .data = "Hello there!    "
};
struct {
    struct ip ip;
    struct icmp icmp;
    char data[128];
} inmsg;

static uint16_t calc_cksum(void *src, size_t len) {
    uint16_t *ptr = (uint16_t *)src;
    uint32_t cks = 0;
    for (; (void *)ptr < src + len; ptr++) {
        cks += *ptr;
    }
    while (cks & 0xffff0000) {
        cks = (cks & 0xffff) + (cks >> 16);
    }
    return ~((uint16_t)cks);
}

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "usage: %s <hostname> <ttl>\n", argv[0]);
        exit(EXIT_FAILURE);
    };

    ttl = atoi(argv[2]);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_DEFAULT | AI_CANONNAME;
    error = getaddrinfo(argv[1], NULL, &hints, &res0);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        exit(EXIT_FAILURE);
    }

    dest = res0;
    for (dest = res0; dest != NULL; dest = dest->ai_next) {
        printf("pinging %s with ttl %d\n", dest->ai_canonname, ttl);

        sock = socket(dest->ai_family, dest->ai_socktype, IPPROTO_ICMP);
        if (sock == -1) {
            perror("socket");
            continue;
        }

        if (setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == -1) {
            perror("setsockopt");
            continue;
        }

        break;
    }

    if (dest == NULL) {
        exit(EXIT_FAILURE);
    }

    struct timeval t0, t1;
    int id = arc4random();
    for (int i = 0; ; i++) {

        gettimeofday(&t0, NULL);

        outmsg.icmp.icmp_type = ICMP_ECHO;
        outmsg.icmp.icmp_id = htons(id);
        outmsg.icmp.icmp_seq = htons(i);

        outmsg.icmp.icmp_cksum = 0;
        int len = sizeof(outmsg.icmp) + strlen(outmsg.data);
        outmsg.icmp.icmp_cksum = calc_cksum((void *)&outmsg, len);

        fprintf(stdout, "ping %d... ", ntohs(outmsg.icmp.icmp_seq));
        fflush(stdout);

        error = sendto(sock, &outmsg, len, 0, dest->ai_addr, dest->ai_addrlen);
        if (error <= 0) {
            perror("sendto");
        } else {
            socklen_t slen = 0;
            len = recvfrom(sock, &inmsg, sizeof(inmsg), 0, NULL, &slen);
            if (len <= 0) {
                perror("recvfrom");
            }
            gettimeofday(&t1, NULL);

            //if (calc_cksum(&inmsg.icmp, len - sizeof(inmsg.ip)) != 0) {}

            char *what = "unknown";
            switch (inmsg.icmp.icmp_type) {
                case ICMP_ECHOREPLY: what = "pong"; break;
                case ICMP_TIMXCEED: what = "timxceed"; break;
            };

            uint32_t td = 1000*(t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec)/1000;
            fprintf(stdout, "%s %d from %s in %u ms\n", what, ntohs(outmsg.icmp.icmp_seq), inet_ntoa(inmsg.ip.ip_src), td);
        }

        usleep(5e5);
    };

    return 0;
}
