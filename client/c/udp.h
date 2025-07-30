#ifndef UDP_H
#define UDP_H

#include <netinet/in.h>

typedef struct {
    int socket_fd;
    struct sockaddr_in server_addr;
} UDPConnection;

UDPConnection connectToUDPServer(int port);
void sendUDPEvent(const char *message, UDPConnection *udpConnection);
void sendUDPBinary(const uint8_t *data, size_t len, UDPConnection *udpConnection);
void closeUDPConnection(UDPConnection *udpConnection);

#endif
