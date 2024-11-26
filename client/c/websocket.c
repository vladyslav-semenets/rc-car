#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include "dualshock.h"
#include "websocket.h"

#define MAX_PAYLOAD_SIZE 1024
#define WEB_SOCKET_ADDRESS "127.0.0.1"
#define WEB_SOCKET_PORT 80

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

    unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
    memset(buf, 0, sizeof(buf));
    strncpy((char *)&buf[LWS_PRE], message, MAX_PAYLOAD_SIZE);

    lws_callback_on_writable(webSocketInstance);
    lws_service(lwsContext, 100);

    printf("Queued message for sending: %s\n", message);
}

struct lws *connectToWebSocketServer(void) {
    struct lws_context_creation_info contextCreationInfo;
    struct lws_client_connect_info connectionInfo;

    memset(&contextCreationInfo, 0, sizeof(contextCreationInfo));
    contextCreationInfo.protocols = (struct lws_protocols[]){{"example-protocol", callbackWebsocket, 0, 0}, {NULL, NULL, 0, 0}};
    contextCreationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    lwsContext = lws_create_context(&contextCreationInfo);
    if (!lwsContext) {
        fprintf(stderr, "Failed to create WebSocket context.\n");
        return NULL;
    }

    memset(&connectionInfo, 0, sizeof(connectionInfo));
    connectionInfo.context = lwsContext;
    connectionInfo.address = WEB_SOCKET_ADDRESS;
    connectionInfo.port = WEB_SOCKET_PORT;
    connectionInfo.path = "/";

    struct lws *wsi = lws_client_connect_via_info(&connectionInfo);

    if (!wsi) {
        fprintf(stderr, "Failed to establish WebSocket connection.\n");
        lws_context_destroy(lwsContext);
        return NULL;
    }

    return wsi;
}

void closeWebSocketServer() {
    lws_context_destroy(lwsContext);
}
