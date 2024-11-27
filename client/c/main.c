#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "dualshock.h"
#include "websocket.h"

int isRunning = 1;

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            closeJoystick();
            closeWebSocketServer();
            exit(0);
        default:
            break;
    }
}

int main() {
    if (initJoystick() != 0) {
        return -1;
    }

    struct sigaction sa;
    struct lws *webSocketInstance = connectToWebSocketServer();

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    startJoystickLoop(&isRunning, webSocketInstance);

    return 0;
}
