#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include "dualshock.h"
#include "websocket.h"

#define MAX_PAYLOAD_SIZE 1024
#define WEB_SOCKET_PORT 8585

struct lws *webSocketInstance = NULL;
struct lws_context *lwsContext = NULL;

static int callbackWebsocket(
    struct lws *wsi,
    const enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            printf("WebSocket connection established.\n");
            webSocketInstance = wsi;
        }
        break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            printf("Received message: %s\n", (char *)in);
        }
        break;

        case LWS_CALLBACK_CLIENT_CLOSED: {
            printf("WebSocket connection closed.\n");
            webSocketInstance = NULL;
        }
        break;

        default:
            break;
    }
    return 0;
}

void sendWebSocketEvent(const char *message, struct lws *webSocketInstance) {
    if (!webSocketInstance) {
        fprintf(stderr, "WebSocket connection is not established.\n");
        return;
    }

    unsigned char *rawMessage = (unsigned char *)message;
    const size_t len = strlen(message);

    lws_write(webSocketInstance, rawMessage, len, LWS_WRITE_TEXT);
}

WebSocketConnection connectToWebSocketServer(void) {
    WebSocketConnection wsConnection = {NULL, NULL};
    struct lws_context_creation_info contextCreationInfo;
    struct lws_client_connect_info connectionInfo;

    memset(&contextCreationInfo, 0, sizeof(contextCreationInfo));
    contextCreationInfo.protocols = (struct lws_protocols[]){{"websocket", callbackWebsocket, 0, 0}, {NULL, NULL, 0, 0}};
    contextCreationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    lwsContext = lws_create_context(&contextCreationInfo);
    if (!lwsContext) {
        fprintf(stderr, "Failed to create WebSocket context.\n");
        return wsConnection;
    }

    memset(&connectionInfo, 0, sizeof(connectionInfo));
    connectionInfo.context = lwsContext;
    connectionInfo.address = getenv("RASPBERRY_PI_IP");
    connectionInfo.port = WEB_SOCKET_PORT;
    connectionInfo.path = "/?source=rc-car-client";

    struct lws *wsi = lws_client_connect_via_info(&connectionInfo);

    if (!wsi) {
        fprintf(stderr, "Failed to establish WebSocket connection.\n");
        lws_context_destroy(lwsContext);
        return wsConnection;
    }

    wsConnection.context = lwsContext;
    wsConnection.wsi = wsi;

    return wsConnection;
}

void closeWebSocketServer() {
    lws_context_destroy(lwsContext);
}
