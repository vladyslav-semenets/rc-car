#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <libwebsockets.h>

struct lws *connectToWebSocketServer();
void closeWebSocketServer();
void sendWebSocketEvent(const char *message, struct lws *webSocketInstance);

#endif
