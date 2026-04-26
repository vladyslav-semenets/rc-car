#include <signal.h>
#include <unistd.h>
#include <stdio.h>
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

    startJoystickLoop(&isRunning, &udpConnection);

    return 0;
}
