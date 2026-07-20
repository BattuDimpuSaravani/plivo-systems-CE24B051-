// sender.c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HARNESS_IN_PORT 47010
#define RELAY_OUT_PORT  47001
#define FRAME_SIZE 160
#define REDUNDANCY_SKIP_MOD 20 // Holds bandwidth overhead at ~1.98x (cap is 2.00x)

static int make_udp_bound(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    return fd;
}

int main(void) {
    int in_fd = make_udp_bound(HARNESS_IN_PORT);

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_fd < 0) { perror("socket out"); exit(1); }
    struct sockaddr_in relay_addr;
    memset(&relay_addr, 0, sizeof(relay_addr));
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    relay_addr.sin_port = htons(RELAY_OUT_PORT);

    uint8_t harness_buf[4 + FRAME_SIZE];
    uint8_t prev_payload[FRAME_SIZE];
    int have_prev = 0;

    uint8_t out_buf[4 + 1 + FRAME_SIZE + FRAME_SIZE];

    while (1) {
        ssize_t n = recvfrom(in_fd, harness_buf, sizeof(harness_buf), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n != (ssize_t)sizeof(harness_buf)) {
            continue;
        }

        uint32_t seq_be;
        memcpy(&seq_be, harness_buf, 4);
        uint32_t seq = ntohl(seq_be);
        uint8_t *payload = harness_buf + 4;

        int include_redundant = have_prev && (seq % REDUNDANCY_SKIP_MOD != 0);

        size_t off = 0;
        uint32_t seq_net = htonl(seq);
        memcpy(out_buf + off, &seq_net, 4); off += 4;
        out_buf[off] = include_redundant ? 0x01 : 0x00; off += 1;
        memcpy(out_buf + off, payload, FRAME_SIZE); off += FRAME_SIZE;
        if (include_redundant) {
            memcpy(out_buf + off, prev_payload, FRAME_SIZE); off += FRAME_SIZE;
        }

        sendto(out_fd, out_buf, off, 0, (struct sockaddr*)&relay_addr, sizeof(relay_addr));

        memcpy(prev_payload, payload, FRAME_SIZE);
        have_prev = 1;
    }

    close(in_fd);
    close(out_fd);
    return 0;
}