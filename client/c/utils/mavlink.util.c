#include "../libs/mavlink/common/mavlink.h"
#include "../udp.h"

const uint8_t systemId = 1;
const uint8_t componentId = 200;

void sendMavlinkCommand(
    const int command,
    UDPConnection *udpConnection,
    const float *params,
    const size_t numParams
) {
    mavlink_message_t message;
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];

    float safeParams[7] = {0};
    if (params != NULL && numParams > 0) {
        size_t copyCount = numParams > 7 ? 7 : numParams;
        memcpy(safeParams, params, copyCount * sizeof(float));
    }

    mavlink_msg_command_long_pack(
        systemId,
        componentId,
        &message,
        1, 1,
        command, 0,
        safeParams[0], safeParams[1], safeParams[2], safeParams[3],
        safeParams[4], safeParams[5], safeParams[6]
    );

    uint16_t len = mavlink_msg_to_send_buffer(buffer, &message);
    sendUDPBinary(buffer, len, udpConnection);
}

