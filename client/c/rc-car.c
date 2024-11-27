#include <stdlib.h>;
#include <stdio.h>;
#include <math.h>;
#include <cjson/cJSON.h>;
#include "rc-car.h";
#include "websocket.h";

struct CommonActionPayload {
    char *to;
};

struct TurnToActionPayloadData {
    const char *degrees;
};

struct ResetTurnsActionPayloadData {
    char *action;
    const char *degreeOfTurns;
};

struct ForwardBackwardActionPayloadData {
    const char *carSpeed;
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

void changDegreeOfTurns(RcCar *self) {
    int len = snprintf(NULL, 0, "%f", self->degreeOfTurns);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", self->degreeOfTurns);

    struct ResetTurnsActionPayloadData resetTurnsActionPayloadData;
    resetTurnsActionPayloadData.degreeOfTurns = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "change-degree-of-turns");
    cJSON_AddStringToObject(data, "degreeOfTurns", resetTurnsActionPayloadData.degreeOfTurns);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(axisXDegreesAsString);
}

void resetTurns(RcCar *self) {
    int len = snprintf(NULL, 0, "%f", self->degreeOfTurns);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", self->degreeOfTurns);

    struct ResetTurnsActionPayloadData resetTurnsActionPayloadData;
    resetTurnsActionPayloadData.degreeOfTurns = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "reset-turns");
    cJSON_AddStringToObject(data, "degreeOfTurns", resetTurnsActionPayloadData.degreeOfTurns);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(axisXDegreesAsString);
}

void turnCar(RcCar *self, const float *degrees) {
    float degreesValue = *degrees;
    int len = snprintf(NULL, 0, "%f", degreesValue);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", degreesValue);

    struct TurnToActionPayloadData turnToActionPayload;
    turnToActionPayload.degrees = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "turn-to");
    cJSON_AddStringToObject(data, "degrees", turnToActionPayload.degrees);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(axisXDegreesAsString);
}

void forward(RcCar *self, const int *speed) {
    int speedValue = *speed;
    int len = snprintf(NULL, 0, "%d", speedValue);
    char *speedAsString = malloc(len + 1);
    snprintf(speedAsString, len + 1, "%d", speedValue);

    struct ForwardBackwardActionPayloadData forwardBackwardActionPayloadData;
    forwardBackwardActionPayloadData.carSpeed = speedAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "forward");
    cJSON_AddStringToObject(data, "carSpeed", forwardBackwardActionPayloadData.carSpeed);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(speedAsString);
}

void backward(RcCar *self, const int *speed) {
    int speedValue = *speed;
    int len = snprintf(NULL, 0, "%d", speedValue);
    char *speedAsString = malloc(len + 1);
    snprintf(speedAsString, len + 1, "%d", speedValue);

    struct ForwardBackwardActionPayloadData forwardBackwardActionPayloadData;
    forwardBackwardActionPayloadData.carSpeed = speedAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "backward");
    cJSON_AddStringToObject(data, "carSpeed", forwardBackwardActionPayloadData.carSpeed);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
    free(speedAsString);
}

void setEscToNeutralPosition(RcCar *self) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "set-esc-to-neutral-position");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
}

void startCamera(RcCar *self) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "start-camera");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
}

void stopCamera(RcCar *self) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "stop-camera");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, self->webSocketInstance);
}

RcCar *newRcCar(struct lws *webSocketInstance) {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->webSocketInstance = webSocketInstance;
    rcCar->degreeOfTurns = 87.0f;
    rcCar->turn = turnCar;
    rcCar->forward = forward;
    rcCar->backward = backward;
    rcCar->setEscToNeutralPosition = setEscToNeutralPosition;
    rcCar->changDegreeOfTurns = changDegreeOfTurns;
    rcCar->startCamera = startCamera;
    rcCar->stopCamera = stopCamera;
    rcCar->resetTurns = resetTurns;
    return rcCar;
}
