#include <stdlib.h>;
#include <stdio.h>;
#include <cjson/cJSON.h>;
#include "rc-car.h";
#include "websocket.h";

struct CommonActionPayload {
    char *to;
};

struct TurnToActionPayloadData {
    char *action;
    const char *degrees;
};

char* prepareActionPayload(cJSON *data) {
    struct CommonActionPayload actionPayload;
    actionPayload.to = "rc-car-server";

    cJSON *base = cJSON_CreateObject();

    cJSON_AddStringToObject(base, "to", actionPayload.to);
    cJSON_AddItemToObject(base, "data", data);

    char *jsonString = cJSON_Print(base);
    cJSON_Delete(base);

    return jsonString;
}

void turnCar(RcCar *self, const float *degrees) {
    int len = snprintf(NULL, 0, "%f", degrees);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", degrees);

    struct TurnToActionPayloadData turnToActionPayload;
    turnToActionPayload.action = "turn-to";
    turnToActionPayload.degrees = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", turnToActionPayload.action);
    cJSON_AddStringToObject(data, "degrees", turnToActionPayload.degrees);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(axisXDegreesAsString);
}

RcCar *newRcCar(struct lws *webSocketInstance) {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->webSocketInstance = webSocketInstance;
    rcCar->turn = turnCar;
    return rcCar;
}
