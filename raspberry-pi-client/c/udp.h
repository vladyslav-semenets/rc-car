// udp.h
#ifndef UDP_H
#define UDP_H

#include "libs/mavlink/common/mavlink.h"

#define BUFFER_LEN 512

typedef void (*MavlinkMessageHandler)(mavlink_message_t *msg);
int createUdpServer(int port, MavlinkMessageHandler onMessage);
void stopUdpServer(void);

#endif
