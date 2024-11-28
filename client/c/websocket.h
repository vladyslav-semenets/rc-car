#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <libwebsockets.h>

typedef struct {
    struct lws_context *context;
    struct lws *wsi;
} WebSocketConnection;

WebSocketConnection connectToWebSocketServer();
void closeWebSocketServer();
void sendWebSocketEvent(const char *message, struct lws *webSocketInstance);

#endif
