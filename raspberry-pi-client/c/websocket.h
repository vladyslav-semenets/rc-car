#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <libwebsockets.h>

typedef struct {
    struct lws_context *context;
    struct lws *wsi;
} WebSocketConnection;

typedef void (*WebSocketEventCallback)(const char *message);

WebSocketConnection connectToWebSocketServer();
void closeWebSocketServer();
void setWebSocketEventCallback(WebSocketEventCallback callback);
void sendWebSocketEvent(const char *message, struct lws *webSocketInstance);

#endif
