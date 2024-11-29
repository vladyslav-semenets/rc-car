#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>;
#include <cjson/cJSON.h>;
#include <stdio.h>

#define MAX_CLIENTS 2
#define MAX_PAYLOAD_SIZE 1024
#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"

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
    size_t valueLength = valueEnd ? (size_t) (valueEnd - keyStart) : strlen(keyStart);

    if (valueLength >= outputSize) {
        return 0;
    }

    strncpy(output, keyStart, valueLength);
    output[valueLength] = '\0';
    return 1;
}

static int callbackWebsocket(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
) {
    char query[256] = {0};
    char source[128] = {0};

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            if (lws_hdr_copy_fragment(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS, 0) > 0) {
                extractQueryValue(query, "source", source, sizeof(source));

                if (clientCount < MAX_CLIENTS) {
                    clients[clientCount].wsi = wsi;
                    snprintf(clients[clientCount].source, sizeof(clients[clientCount].source), "%s", source);
                    clientCount++;
                } else {
                    lws_close_reason(wsi, LWS_CLOSE_STATUS_GOINGAWAY, NULL, 0);
                }
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            cJSON *message = cJSON_Parse(in);
            if (message) {
                const cJSON *to = cJSON_GetObjectItemCaseSensitive(message, "to");
                if (cJSON_IsString(to) && to->valuestring) {
                    for (int i = 0; i < clientCount; i++) {
                        if (strcmp(clients[i].source, to->valuestring) == 0) {
                            char *newMessageJSON = cJSON_Print(message);
                            if (newMessageJSON) {
                                const size_t newMessageLen = strlen(newMessageJSON);
                                char *out = NULL;

                                out = (char *) malloc(
                                    sizeof(char) * (
                                        LWS_SEND_BUFFER_PRE_PADDING + newMessageLen + LWS_SEND_BUFFER_POST_PADDING));
                                memcpy(out + LWS_SEND_BUFFER_PRE_PADDING, newMessageJSON, newMessageLen);

                                lws_write(clients[i].wsi, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

                                printf(KBLU"[websocket_write to %s] %s\n"RESET, to->valuestring, newMessageJSON);
                                free(out);
                            }
                        }
                    }
                }
                cJSON_Delete(message);
            }
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            for (int i = 0; i < clientCount; i++) {
                if (clients[i].wsi == wsi) {
                    for (int j = i; j < clientCount - 1; j++) {
                        clients[j] = clients[j + 1];
                    }
                    clientCount--;
                    memset(&clients[clientCount], 0, sizeof(Clients));
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
    contextCreationInfo.protocols = (struct lws_protocols[]){
        {"websocket", callbackWebsocket, 0, 0}, {NULL, NULL, 0, 0}
    };

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
