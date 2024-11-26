#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <cjson/cJSON.h>;
#include <stdio.h>

#define MAX_CLIENTS 2
#define MAX_PAYLOAD_SIZE 1024

int isRunning = 1;
struct lws_context *lwsContext = NULL;

typedef struct {
    struct lws *wsi;
    char source[128];
} Clients;

static Clients clients[MAX_CLIENTS];
static int clientCount = 0;

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            lws_context_destroy(lwsContext);
            exit(0);
        default:
            break;
    }
}

int extractQueryValue(const char *queryString, const char *key, char *output, size_t outputSize) {
    if (!queryString || !key || !output || outputSize == 0) {
        return 0;
    }

    const char *keyStart = strstr(queryString, key);
    if (!keyStart) {
        return 0;
    }

    keyStart += strlen(key);
    if (*keyStart != '=') {
        return 0;
    }

    keyStart++;
    const char *valueEnd = strchr(keyStart, '&');
    size_t valueLength = valueEnd ? (size_t)(valueEnd - keyStart) : strlen(keyStart);

    if (valueLength >= outputSize) {
        return 0;
    }

    strncpy(output, keyStart, valueLength);
    output[valueLength] = '\0';
    return 1;
}

static int callbackWebsocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    char query[256] = {0};
    char source[128] = {0};

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        if (lws_hdr_copy_fragment(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS, 0) > 0) {
            extractQueryValue(query, "source", source, sizeof(source));
        }

        if (clientCount < MAX_CLIENTS) {
            clients[clientCount].wsi = wsi;
            snprintf(clients[clientCount].source, sizeof(clients[clientCount].source), "%s", source);
            clientCount++;
            const char *msg = "{\"message\":\"Connection Established\"}";

            unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
            memset(buf, 0, sizeof(buf));

            snprintf((char *)buf + LWS_PRE, MAX_PAYLOAD_SIZE, "%s", msg);
            lws_write(wsi, buf + LWS_PRE, strlen(msg), LWS_WRITE_TEXT);
        } else {
            lws_close_reason(wsi, LWS_CLOSE_STATUS_GOINGAWAY, NULL, 0);
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        const cJSON *to = NULL;
        cJSON *message = cJSON_Parse(in);
        to = cJSON_GetObjectItemCaseSensitive(message, "to");

        if (cJSON_IsString(to) && (to->valuestring != NULL)) {
            for (int i = 0; i < clientCount; i++) {
                if (strcmp(clients[i].source, to->valuestring) == 0) {
                    char *newMessageJSON = cJSON_Print(message);
                    lws_write(clients[i].wsi, (unsigned char *)newMessageJSON, strlen(newMessageJSON), LWS_WRITE_TEXT);
                    free(newMessageJSON);
                }
            }
        }

        cJSON_Delete(message);

        break;
    }

    case LWS_CALLBACK_CLOSED: {
        printf("Client disconnected\n");

        for (int i = 0; i < clientCount; i++) {
            if (clients[i].wsi == wsi) {
                for (int j = i; j < clientCount - 1; j++) {
                    clients[j] = clients[j + 1];
                }
                clientCount--;
                break;
            }
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

int main() {
    struct lws_context_creation_info contextCreationInfo;
    memset(&contextCreationInfo, 0, sizeof(contextCreationInfo));
    contextCreationInfo.port = 8585;
    contextCreationInfo.protocols = (struct lws_protocols[]){{"websocket", callbackWebsocket, 0, 0}, {NULL, NULL, 0, 0}};

    lwsContext = lws_create_context(&contextCreationInfo);
    if (!lwsContext) {
        printf("Failed to create WebSocket context\n");
        return -1;
    }

    printf("WebSocket server started on port %d\n", contextCreationInfo.port);

    while (isRunning) {
        lws_service(lwsContext, 1000);

        if (!isRunning) {
           break;
        }
    }
}
