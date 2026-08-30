#ifndef MAVLINK_UTIL_H
#define MAVLINK_UTIL_H
#include "../udp.h"
void sendMavlinkCommand(int command, UDPConnection *udpConnection, const float *params, const size_t numParams);
#endif //MAVLINK_UTIL_H
