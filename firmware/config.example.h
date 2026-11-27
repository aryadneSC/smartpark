#pragma once
const char* API_BASE = "LINK_WEB_SEM_BARRA_NO_FINAL";
const char* DEVICE_ID = "esp32-centro-01";
const char* DEVICE_SECRET = "GERE_UM_SEGREDO_FORTE";
static const String ENDPOINT_ACESSO =
    String(API_BASE) + "/api/iot/access-request";

static const String ENDPOINT_TELEMETRIA =
    String(API_BASE) + "/api/iot/telemetry";


const char* ROOT_CA = R"PEM(
)PEM";