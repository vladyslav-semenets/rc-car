#include <signal.h>
#include <unistd.h>
#include "libs/env/dotenv.h"
#include "joystick.h"
#include "websocket.h"
#include "udp.h"
#include "rc-car.h"
#include "utils//mavlink.util.h"

int isRunning = 1;
UDPConnection udpConnection;

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

int main() {
    env_load(".env", false);

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

    float params[] = {};

    sendMavlinkCommand(MAVLINK_START_CAMERA_COMMAND, &udpConnection, params, 0);

    startJoystickLoop(&isRunning, &udpConnection);

    return 0;
}
