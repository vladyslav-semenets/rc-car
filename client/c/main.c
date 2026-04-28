#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include "libs/env/dotenv.h"
#include "joystick.h"
#include "websocket.h"
#include "udp.h"
#include "rc-car.h"
#include "utils//mavlink.util.h"
#include "libs/mavlink/common/mavlink.h"

int isRunning = 1;
UDPConnection udpConnection;

// ── Heartbeat поток ───────────────────────────────────────────────────────────
// Шлёт MAVLink HEARTBEAT каждые 300мс.
// Pi watchdog ждёт его — если тишина > 1 сек, машина останавливается.
static pthread_t heartbeatThreadHandle;

static void *heartbeatThread(void *arg) {
    UDPConnection *conn = (UDPConnection *)arg;
    printf("[Heartbeat] started, interval=300ms\n");

    while (isRunning) {
        mavlink_message_t msg;
        uint8_t buffer[MAVLINK_MAX_PACKET_LEN];

        mavlink_msg_heartbeat_pack(
            1, 200, &msg,
            MAV_TYPE_GROUND_ROVER,
            MAV_AUTOPILOT_GENERIC,
            0, 0,
            MAV_STATE_ACTIVE
        );

        uint16_t len = mavlink_msg_to_send_buffer(buffer, &msg);
        sendUDPBinary(buffer, len, conn);

        usleep(300000);  // 300мс
    }
    return NULL;
}

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            closeJoystick();
            closeUDPConnection(&udpConnection);
            exit(0);
        default:
            break;
    }
}

void startRemoteMediaMTX() {
    const char *ip   = getenv("RASPBERRY_PI_IP");
    const char *user = getenv("PI_SSH_USER");
    if (!ip || !user) {
        fprintf(stderr, "[SSH] RASPBERRY_PI_IP or PI_SSH_USER not set in .env — skipping mediamtx start\n");
        return;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no %s@%s "
        "'pgrep mediamtx || (cd ~/rc-car-repo && ./mediamtx)' > /dev/null 2>&1 &",
        user, ip);
    printf("[SSH] Starting mediamtx on %s@%s...\n", user, ip);
    system(cmd);
}

int main() {
    env_load(".env", false);

    startRemoteMediaMTX();

    if (initJoystick() != 0) {
        return -1;
    }

    udpConnection = connectToUDPServer(atoi(getenv("UDP_SERVER_PORT")));

    if (udpConnection.socket_fd < 0) {
        fprintf(stderr, "[UDP] Failed to setup connection\n");
        closeJoystick();
        return -1;
    }

    struct sigaction sa;

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    // Запускаем heartbeat поток
    if (pthread_create(&heartbeatThreadHandle, NULL, heartbeatThread, &udpConnection) != 0) {
        fprintf(stderr, "[Heartbeat] failed to create thread\n");
        closeJoystick();
        closeUDPConnection(&udpConnection);
        return -1;
    }

    startJoystickLoop(&isRunning, &udpConnection);

    return 0;
}
