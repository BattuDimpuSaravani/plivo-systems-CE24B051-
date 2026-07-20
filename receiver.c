// receiver.c
#define _POSIX_C_SOURCE 200809L
// Listens on 127.0.0.1:47002 for packets from the relay (our wire format, see sender.c),
// stores frames in a ring-buffer jitter buffer keyed by seq, recovers single losses from
// the piggybacked redundant copy, and plays out one frame every 20ms on a wall-clock
// schedule anchored at T0 + DELAY_MS, forwarding to the harness player on 127.0.0.1:47020
// in the harness's own format (4-byte BE seq + 160-byte payload).
//
// Env vars provided by the harness: T0 (unix epoch seconds, float), DURATION_S, DELAY_MS.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define RELAY_IN_PORT     47002
#define HARNESS_OUT_PORT  47020
#define FRAME_SIZE 160
#define IN_PKT_MAX (4 + 1 + FRAME_SIZE + FRAME_SIZE)

#define BUF_SLOTS 512 // ring buffer size; must exceed max reorder/jitter depth in frames

typedef struct {
    int has_data;
    uint32_t seq; // seq stored to disambiguate ring-buffer wraparound aliasing
    uint8_t payload[FRAME_SIZE];
} Slot;

static Slot g_buf[BUF_SLOTS];

static void buf_put(uint32_t seq, const uint8_t *payload) {
    Slot *s = &g_buf[seq % BUF_SLOTS];
    if (s->has_data && s->seq == seq) return; // already have it (dup), don't bother rewriting
    s->has_data = 1;
    s->seq = seq;
    memcpy(s->payload, payload, FRAME_SIZE);
}

// Returns 1 and fills out_payload if frame `seq` is present, else 0.
static int buf_get(uint32_t seq, uint8_t *out_payload) {
    Slot *s = &g_buf[seq % BUF_SLOTS];
    if (s->has_data && s->seq == seq) {
        memcpy(out_payload, s->payload, FRAME_SIZE);
        return 1;
    }
    return 0;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    const char *t0_str = getenv("T0");
    const char *delay_str = getenv("DELAY_MS");
    const char *dur_str = getenv("DURATION_S");
    double t0 = t0_str ? atof(t0_str) : now_seconds();
    double delay_ms = delay_str ? atof(delay_str) : 100.0;
    double duration_s = dur_str ? atof(dur_str) : 60.0;

    fprintf(stderr, "[receiver DEBUG] T0=%s DELAY_MS=%s DURATION_S=%s\n",
            t0_str ? t0_str : "(null)", delay_str ? delay_str : "(null)",
            dur_str ? dur_str : "(null)");
    fprintf(stderr, "[receiver DEBUG] parsed t0=%.6f delay_ms=%.2f duration_s=%.2f now=%.6f (now-t0=%.3f)\n",
            t0, delay_ms, duration_s, now_seconds(), now_seconds() - t0);

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (in_fd < 0) { perror("socket"); exit(1); }
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind_addr.sin_port = htons(RELAY_IN_PORT);
    if (bind(in_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind"); exit(1);
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_fd < 0) { perror("socket out"); exit(1); }
    struct sockaddr_in harness_addr;
    memset(&harness_addr, 0, sizeof(harness_addr));
    harness_addr.sin_family = AF_INET;
    harness_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    harness_addr.sin_port = htons(HARNESS_OUT_PORT);

    uint8_t in_pkt[IN_PKT_MAX];

    // Frame 0's deadline is t0 + delay_ms/1000; frame i's deadline is that + i*20ms.
    double delay_s = delay_ms / 1000.0;
    long total_frames = (long)(duration_s / 0.020) + 50; // small safety margin

    uint32_t play_idx = 0;
    uint8_t out_payload[FRAME_SIZE];

    while (play_idx < (uint32_t)total_frames) {
        double deadline = t0 + delay_s + (double)play_idx * 0.020;

        // Drain any packets that have arrived, until the deadline.
        while (1) {
            double remaining = deadline - now_seconds();
            if (remaining <= 0) break;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(in_fd, &rfds);
            struct timeval tv;
            tv.tv_sec = (time_t)remaining;
            tv.tv_usec = (suseconds_t)((remaining - (double)tv.tv_sec) * 1e6);
            int rv = select(in_fd + 1, &rfds, NULL, NULL, &tv);
            if (rv <= 0) break; // timeout or error -> move on to deadline check
            ssize_t n = recvfrom(in_fd, in_pkt, sizeof(in_pkt), 0, NULL, NULL);
            if (n < 5) continue; // malformed
            uint32_t seq_be; memcpy(&seq_be, in_pkt, 4);
            uint32_t seq = ntohl(seq_be);
            uint8_t flags = in_pkt[4];
            const uint8_t *cur = in_pkt + 5;
            if ((size_t)n < 5 + FRAME_SIZE) continue;
            buf_put(seq, cur);
            if ((flags & 0x01) && (size_t)n >= 5 + FRAME_SIZE + FRAME_SIZE && seq > 0) {
                const uint8_t *prev = in_pkt + 5 + FRAME_SIZE;
                buf_put(seq - 1, prev); // recover previous frame if we don't have it yet
            }
        }

        // Deadline reached: emit frame play_idx if we have it, else it's a miss.
        if (buf_get(play_idx, out_payload)) {
            uint8_t out_pkt[4 + FRAME_SIZE];
            uint32_t seq_net = htonl(play_idx);
            memcpy(out_pkt, &seq_net, 4);
            memcpy(out_pkt + 4, out_payload, FRAME_SIZE);
            ssize_t sent = sendto(out_fd, out_pkt, sizeof(out_pkt), 0,
                   (struct sockaddr*)&harness_addr, sizeof(harness_addr));
            if (sent < 0) {
                // A UDP socket can get "poisoned" by a delayed ICMP error from
                // an earlier send (e.g. before the harness player finished
                // binding); the fix is to drop it and open a fresh one.
                close(out_fd);
                out_fd = socket(AF_INET, SOCK_DGRAM, 0);
                sent = sendto(out_fd, out_pkt, sizeof(out_pkt), 0,
                       (struct sockaddr*)&harness_addr, sizeof(harness_addr));
                if (sent < 0) {
                    fprintf(stderr, "[receiver DEBUG] sendto FAILED twice frame %u: %s\n",
                            play_idx, strerror(errno));
                }
            } else if ((size_t)sent != sizeof(out_pkt)) {
                fprintf(stderr, "[receiver DEBUG] sendto SHORT WRITE frame %u: sent %zd of %zu\n",
                        play_idx, sent, sizeof(out_pkt));
            } else if (play_idx < 10 || play_idx % 100 == 0) {
                fprintf(stderr, "[receiver DEBUG] sent frame %u OK (%zd bytes)\n", play_idx, sent);
            }
        }
        else if (play_idx < 10 || play_idx % 100 == 0) {
            fprintf(stderr, "[receiver DEBUG] MISS frame %u at deadline (now-deadline=%.3f)\n",
                    play_idx, now_seconds() - deadline);
        }

        play_idx++;
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
