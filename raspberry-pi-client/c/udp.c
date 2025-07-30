#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include "udp.h"

static int sockfd = -1;
static volatile int isUdpRunning = 0;

int createUdpServer(int port, MavlinkMessageHandler onMessage) {
    struct sockaddr_in addr, sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    uint8_t buf[BUFFER_LEN];
    ssize_t recv_len;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[UDP] socket creation failed");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[UDP] bind failed");
        close(sockfd);
        sockfd = -1;
        return -1;
    }

    struct timeval timeout = {1, 0}; // 1 second timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("[UDP] setsockopt failed");
        close(sockfd);
        sockfd = -1;
        return -1;
    }

    printf("[UDP] Listening for MAVLink packets on port %d\n", port);

    isUdpRunning = 1;

    mavlink_message_t msg;
    mavlink_status_t status;

    while (isUdpRunning) {
        recv_len = recvfrom(sockfd, buf, BUFFER_LEN, 0,
                            (struct sockaddr *)&sender_addr, &sender_len);
        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, just continue listening
                continue;
            } else if (errno == EINTR) {
                // Interrupted by signal, break loop cleanly
                printf("[UDP] recvfrom interrupted by signal\n");
                break;
            } else {
                perror("[UDP] recvfrom failed");
                break;
            }
        }

        for (ssize_t i = 0; i < recv_len; i++) {
            if (mavlink_parse_char(MAVLINK_COMM_0, buf[i], &msg, &status)) {
                if (onMessage) {
                    onMessage(&msg);
                }
            }
        }
    }

    close(sockfd);
    sockfd = -1;
    printf("[UDP] server stopped\n");

    return 0;
}

void stopUdpServer(void) {
    isUdpRunning = 0;
    if (sockfd != -1) {
        printf("[UDP] Closing UDP socket\n");
        close(sockfd);
        sockfd = -1;
    }
}
