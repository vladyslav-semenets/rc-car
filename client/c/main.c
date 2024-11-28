#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "libs/env/dotenv.h"
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

void *webSocketThread(void *arg) {
    struct lws_context *context = (struct lws_context *)arg;

    while (isRunning) {
        lws_service(context, 100);

        if (!isRunning) {
            break;
        }
    }

    return NULL;
}

int main() {
    env_load(".env", false);

    if (initJoystick() != 0) {
        return -1;
    }

    struct sigaction sa;
    WebSocketConnection webSocketConnection = connectToWebSocketServer();

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    pthread_t wsThread;
    pthread_create(&wsThread, NULL, webSocketThread, webSocketConnection.context);

    startJoystickLoop(&isRunning, webSocketConnection.wsi);

    return 0;
}
