#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include "dualshock.h"
#include "websocket.h"

#define MAX_PAYLOAD_SIZE 1024
#define WEB_SOCKET_PORT 8585
#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"

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
    if (message == NULL || webSocketInstance == NULL) {
        return;
    }

    const size_t len = strlen(message);
    char *out = NULL;

    out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING));
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, message, len );

    lws_write(webSocketInstance, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

    printf(KBLU"[websocket_write_back] %s\n"RESET, message);
    free(out);
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
    connectionInfo.address = "100.108.40.34";
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
