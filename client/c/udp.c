#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Mac-specific includes
#ifdef __APPLE__
#include <sys/types.h>
#endif

#include "udp.h"

UDPConnection connectToUDPServer(int port) {
    UDPConnection udpConnection = {-1, {0}};

    // Create UDP socket
    udpConnection.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpConnection.socket_fd < 0) {
        fprintf(stderr, "Failed to create UDP socket.\n");
        return udpConnection;
    }

    // Configure server address
    udpConnection.server_addr.sin_family = AF_INET;
    udpConnection.server_addr.sin_port = htons(port);

    const char *server_ip = getenv("RASPBERRY_PI_IP");
    if (server_ip == NULL) {
        fprintf(stderr, "RASPBERRY_PI_IP environment variable not set.\n");
        close(udpConnection.socket_fd);
        udpConnection.socket_fd = -1;
        return udpConnection;
    }

    if (inet_pton(AF_INET, server_ip, &udpConnection.server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(udpConnection.socket_fd);
        udpConnection.socket_fd = -1;
        return udpConnection;
    }

    printf("UDP connection configured for %s:%d\n", server_ip, port);
    return udpConnection;
}

void sendUDPEvent(const char *message, UDPConnection *udpConnection) {
    if (message == NULL || udpConnection == NULL || udpConnection->socket_fd < 0) {
        return;
    }

    ssize_t sent_bytes = sendto(udpConnection->socket_fd, message, strlen(message), 0,
                               (struct sockaddr*)&udpConnection->server_addr,
                               sizeof(udpConnection->server_addr));

    if (sent_bytes < 0) {
        perror("Failed to send UDP message");
    } else {
        printf("[udp_send] %s\n", message);
    }
}

void sendUDPBinary(const uint8_t *data, size_t len, UDPConnection *udpConnection) {
    if (data == NULL || udpConnection == NULL || udpConnection->socket_fd < 0) {
        return;
    }

    ssize_t sent_bytes = sendto(udpConnection->socket_fd, data, len, 0,
                               (struct sockaddr*)&udpConnection->server_addr,
                               sizeof(udpConnection->server_addr));

    if (sent_bytes < 0) {
        perror("[UDP] Failed to send binary data");
    } else {
        printf("[UDP SEND BINARY] Sent %zd bytes\n", sent_bytes);
    }
}

void closeUDPConnection(UDPConnection *udpConnection) {
    if (udpConnection != NULL && udpConnection->socket_fd >= 0) {
        close(udpConnection->socket_fd);
        udpConnection->socket_fd = -1;
    }
}
