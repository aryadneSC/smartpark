// SmartPark IoT - HTTPS validado (fail-closed), diagnostico por classe de falha,
// Wi-Fi/NTP nao bloqueantes, backoff com jitter, debounce/filtro de entradas.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <esp_system.h>
#include <Wire.h>
#include <time.h>
#include "config.h"

// Nomes, pinos e angulos mantidos do sketch fornecido pela equipe.
#define PIN_SERVO 18
#define PIN_ECHO_HCSR04 34
#define PIN_TRIG_HCSR04 33
#define PIN_LED_VERM 4
#define PIN_LED_VERD 5
#define PIN_BTN_1 14
#define PIN_BTN_2 27
#define PIN_LCD_SDA 21
#define PIN_LCD_SCL 22
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define CANCELA_ABERTA 180
#define CANCELA_FECHADA 60
// Segundo sensor complementar do diagrama; os pinos originais permanecem.
#define PIN_TRIG_SAIDA 25
#define PIN_ECHO_SAIDA 26

typedef enum { ABERTA, FECHADA } Cancela_Status_t;
void conectar_wifi(const char *ssid, const char *password);
void init_pins();
void init_cancela(Cancela_Status_t *status, int pin_servo, Servo *servo);
void mover_cancela(Cancela_Status_t *status, Cancela_Status_t definir_status, Servo *servo);

// ---------------------------------------------------------------------------
// Parametros de tempo e limites (ms, salvo indicacao)
// ---------------------------------------------------------------------------
const unsigned long OPEN_HOLD_MS = 2000;            // cancela aberta apos autorizacao
const unsigned long WIFI_BOOT_GRACE_MS = 20000;     // conectar no boot nao e falha
const unsigned long WIFI_RECONNECT_MS = 10000;
const unsigned long NTP_TIMEOUT_MS = 20000;         // espera NTP antes de marcar falha
const unsigned long NTP_RETRY_MS = 30000;           // nova tentativa apos falha NTP
const unsigned long TELEMETRY_INTERVAL_MS = 15000;  // cadencia normal
const unsigned long TELEMETRY_MIN_GAP_MS = 5500;    // backend exige >= 5 s entre telemetrias
const unsigned long TELEMETRY_BACKOFF_MIN_MS = 6000;
const unsigned long TELEMETRY_BACKOFF_MAX_MS = 60000;
const unsigned long ACCESS_BACKOFF_MIN_MS = 2000;
const unsigned long ACCESS_BACKOFF_MAX_MS = 30000;
const uint8_t ACCESS_MAX_ATTEMPTS = 3;
const unsigned long EVENT_TTL_MS = 90000;           // evento pendente velho nao abre cancela
const uint8_t EVENT_RATE_MAX = 25;                  // backend aceita 30 novos eventos/min
const unsigned long EVENT_RATE_WINDOW_MS = 60000;
const unsigned long STAT_INTERVAL_MS = 60000;
const unsigned long DEBOUNCE_MS = 40;
const float DIST_ON_CM = 15.0f;                     // limiar nominal de presenca
const float DIST_OFF_CM = 18.0f;                    // histerese para liberar
const uint8_t SENSOR_CONFIRM = 3;                   // leituras consecutivas
const uint32_t CONNECT_TIMEOUT_MS = 8000, HTTP_TIMEOUT_MS = 8000;
const uint32_t TLS_HANDSHAKE_TIMEOUT_S = 15;
const uint32_t MIN_HEAP_FOR_TLS = 32768;
const size_t RESP_MAX = 512;                        // corpo maximo aceito
const size_t RES_MAX_LEN = 64;                      // codigo SP-... (backend aceita ate 100)
const size_t EVENT_ID_LEN = 40;

// ---------------------------------------------------------------------------
// Classificacao centralizada de falhas
// ---------------------------------------------------------------------------
enum class Fail : uint8_t {
  None, Wifi, Ntp, CfgCa, CfgUrl, CfgDevice, Mem, Dns, Tcp, Timeout, Tls, Redirect,
  Proxy, Json, Auth, Forbidden, NotFound, Rate, Rejected, Http4, Http5
};
struct ApiResult { Fail fail; int code; };  // code: status HTTP, erro nativo ou 0

// Texto de ate 16 caracteres para a linha 2 do LCD.
const char *failLcd(Fail f) {
  switch (f) {
    case Fail::Wifi: return "Sem WiFi";
    case Fail::Ntp: return "Falha NTP";
    case Fail::CfgCa: return "Cfg CA invalida";
    case Fail::CfgUrl: return "Cfg URL invalida";
    case Fail::CfgDevice: return "Cfg dispositivo";
    case Fail::Mem: return "Memoria baixa";
    case Fail::Dns: return "Falha DNS";
    case Fail::Tcp: return "TCP/tunel falhou";
    case Fail::Timeout: return "Timeout API";
    case Fail::Tls: return "Falha TLS/cert";
    case Fail::Redirect: return "Redirect proxy";
    case Fail::Proxy: return "Resp inesperada";
    case Fail::Json: return "JSON invalido";
    case Fail::Auth: return "401 segredo/id";
    case Fail::Forbidden: return "403 device";
    case Fail::NotFound: return "404 rota";
    case Fail::Rate: return "429 limite";
    case Fail::Rejected: return "Req. rejeitada";
    case Fail::Http4: return "HTTP 4xx";
    case Fail::Http5: return "HTTP 5xx";
    default: return "Erro";
  }
}
// Rotulo curto e estavel para Serial/telemetria (sem segredos).
const char *failTag(Fail f) {
  switch (f) {
    case Fail::Wifi: return "WIFI";
    case Fail::Ntp: return "NTP";
    case Fail::CfgCa: return "CFG_CA";
    case Fail::CfgUrl: return "CFG_URL";
    case Fail::CfgDevice: return "CFG_DEVICE";
    case Fail::Mem: return "MEM";
    case Fail::Dns: return "DNS";
    case Fail::Tcp: return "TCP";
    case Fail::Timeout: return "TIMEOUT";
    case Fail::Tls: return "TLS";
    case Fail::Redirect: return "REDIRECT";
    case Fail::Proxy: return "PROXY";
    case Fail::Json: return "JSON";
    case Fail::Auth: return "HTTP401";
    case Fail::Forbidden: return "HTTP403";
    case Fail::NotFound: return "HTTP404";
    case Fail::Rate: return "HTTP429";
    case Fail::Rejected: return "REJEITADO";
    case Fail::Http4: return "HTTP4XX";
    case Fail::Http5: return "HTTP5XX";
    default: return "OK";
  }
}
// Falhas de configuracao/credencial/rota nao melhoram com nova tentativa.
bool failRetryable(Fail f) {
  switch (f) {
    case Fail::CfgCa: case Fail::CfgUrl: case Fail::CfgDevice: case Fail::Redirect:
    case Fail::Auth: case Fail::Forbidden: case Fail::NotFound: case Fail::Rejected:
    case Fail::Http4: case Fail::None:
      return false;
    default:
      return true;
  }
}
Fail classifyHttp(int code) {
  switch (code) {
    case 200: return Fail::None;
    case 301: case 302: case 303: case 307: case 308: return Fail::Redirect;
    case 400: case 409: case 422: return Fail::Rejected;
    case 401: return Fail::Auth;
    case 403: return Fail::Forbidden;
    case 404: return Fail::NotFound;
    case 408: return Fail::Timeout;
    case 429: return Fail::Rate;
    default: break;
  }
  if (code >= 500) return Fail::Http5;
  if (code >= 400) return Fail::Http4;
  return Fail::Proxy;  // 1xx/2xx/3xx inesperados: nunca sucesso
}
// Erros nativos do HTTPClient (<= 0). Codigos mbedTLS ficam abaixo de -0x1000;
// -1 com erro TLS zero/-1 indica falha de TCP (recusada ou timeout de conexao).
Fail classifyTransport(int code, int tlsCode, unsigned long elapsedMs) {
  if (code == -8) return Fail::Mem;                    // HTTPC_ERROR_TOO_LESS_RAM
  if (code == -11) return Fail::Timeout;               // HTTPC_ERROR_READ_TIMEOUT
  if (tlsCode <= -0x1000) return Fail::Tls;
  if (code == -1 && elapsedMs + 500 >= CONNECT_TIMEOUT_MS) return Fail::Timeout;
  return Fail::Tcp;                                    // -1 recusada, -4/-5 sem conexao
}

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo cancela;
Cancela_Status_t cancela_status = FECHADA;
JsonDocument reqDoc, respDoc;  // reutilizados para evitar alocacoes repetidas

char state[20] = "INICIALIZANDO";
char shownDetail[24] = "";
char reservation[RES_MAX_LEN + 1] = "";
char pendingEvent[EVENT_ID_LEN] = "";
char pendingType[6] = "";
char lastErrTag[32] = "";
unsigned long pendingCreatedAt = 0, pendingNextAt = 0;
uint8_t attempts = 0;
unsigned long openedAt = 0, statusAt = 0, bootAt = 0, lastWifi = 0, lastStat = 0;
unsigned long failedEvents = 0;
bool maintenance = false, apiReady = false, reportDirty = false;
bool bothWas = false, entryWas = false, exitWas = false, conflictWas = false;

// Wi-Fi / NTP
bool wifiStarted = false, wifiUp = false, wifiEverUp = false;
wl_status_t lastWifiStatus = WL_NO_SHIELD;
enum NtpState : uint8_t { NTPS_IDLE, NTPS_WAIT, NTPS_FAIL, NTPS_OK };
NtpState ntpState = NTPS_IDLE;
unsigned long ntpMarkAt = 0;

// Telemetria
bool telemetryAttempted = false, telemetryUrgent = true;
unsigned long lastTelemetryAt = 0, nextTelemetryAt = 0;
uint8_t telemetryFails = 0;

// Limite local de novos eventos (rolling 60 s)
unsigned long eventStamps[EVENT_RATE_MAX];
uint8_t eventHead = 0, eventCount = 0;

// Entradas
struct Debounce { bool raw = false, stable = false; unsigned long changedAt = 0; };
struct Presence { bool active = false; uint8_t count = 0; };
Debounce btnInDeb, btnOutDeb;
Presence presIn, presOut;

// ---------------------------------------------------------------------------
// Funcoes originais: Wi-Fi, pinos e servo
// ---------------------------------------------------------------------------
void conectar_wifi(const char *ssid, const char *password) {
  if (WiFi.status() == WL_CONNECTED) return;
  lastWifi = millis();
  if (!wifiStarted) {
    wifiStarted = true;
    WiFi.begin(ssid, password, 6);
  } else {
    WiFi.reconnect();
  }
}
void init_pins() {
  pinMode(PIN_ECHO_HCSR04, INPUT); pinMode(PIN_TRIG_HCSR04, OUTPUT);
  pinMode(PIN_ECHO_SAIDA, INPUT); pinMode(PIN_TRIG_SAIDA, OUTPUT);
  pinMode(PIN_LED_VERM, OUTPUT); pinMode(PIN_LED_VERD, OUTPUT);
  pinMode(PIN_BTN_1, INPUT_PULLUP); pinMode(PIN_BTN_2, INPUT_PULLUP);
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
}
void mover_cancela(Cancela_Status_t *status, Cancela_Status_t definir_status, Servo *servo) {
  servo->write(definir_status == ABERTA ? CANCELA_ABERTA : CANCELA_FECHADA);
  *status = definir_status;
  digitalWrite(PIN_LED_VERD, definir_status == ABERTA);
  digitalWrite(PIN_LED_VERM, definir_status == FECHADA);
}
void init_cancela(Cancela_Status_t *status, int pin_servo, Servo *servo) {
  servo->attach(pin_servo, 500, 2400);
  mover_cancela(status, FECHADA, servo);
}
void closed() {
  mover_cancela(&cancela_status, FECHADA, &cancela);
  openedAt = 0;
}

// ---------------------------------------------------------------------------
// Utilitarios
// ---------------------------------------------------------------------------
// Estados enviados ao backend: INICIALIZANDO, PRONTO, VALIDANDO, ACESSO_LIBERADO,
// ACESSO_NEGADO, FALHA_REDE e MANUTENCAO. O detalhe (linha 2) so aparece no LCD/Serial.
void show(const char *next, const char *detail = "") {
  if (!strcmp(state, next) && !strcmp(shownDetail, detail)) return;
  Serial.printf("%s%s%s\n", next, detail[0] ? " | " : "", detail);
  snprintf(state, sizeof(state), "%s", next);
  snprintf(shownDetail, sizeof(shownDetail), "%s", detail);
  statusAt = millis();
  char line[17];
  lcd.clear();
  snprintf(line, sizeof(line), "%.16s", next); lcd.print(line);
  lcd.setCursor(0, 1);
  snprintf(line, sizeof(line), "%.16s", detail); lcd.print(line);
}
// Em manutencao, falhas de rede aparecem como MANUTENCAO (detalhe preservado).
void showNet(const char *st, const char *detail) { show(maintenance ? "MANUTENCAO" : st, detail); }
bool reached(unsigned long now, unsigned long t) { return (long)(now - t) >= 0; }
bool timeSynced() { return time(nullptr) > 1700000000; }
// Backoff exponencial limitado; jitter apenas somado (+0..20%) para nunca ficar abaixo do minimo.
unsigned long backoffDelay(uint8_t failures, unsigned long minMs, unsigned long maxMs) {
  unsigned long d = minMs;
  for (uint8_t i = 1; i < failures && d < maxMs; i++) d <<= 1;
  if (d > maxMs) d = maxMs;
  return d + (esp_random() % (d / 5 + 1));
}
const char *wifiName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONN_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "OUTRO";
  }
}
float distanceCM(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10); digitalWrite(trig, LOW);
  unsigned long duration = pulseIn(echo, HIGH, 25000);
  return duration ? duration * 0.0343 / 2 : 999;
}
// Debounce temporal: so aceita a mudanca apos DEBOUNCE_MS estavel.
bool debounceUpdate(Debounce &d, bool raw, unsigned long now) {
  if (raw != d.raw) { d.raw = raw; d.changedAt = now; }
  else if (raw != d.stable && now - d.changedAt >= DEBOUNCE_MS) d.stable = raw;
  return d.stable;
}
// Histerese (liga < 15 cm, desliga >= 18 cm) + confirmacao por leituras consecutivas.
bool presenceUpdate(Presence &p, float cm) {
  bool flip = p.active ? (cm >= DIST_OFF_CM) : (cm < DIST_ON_CM);
  if (!flip) p.count = 0;
  else if (++p.count >= SENSOR_CONFIRM) { p.active = !p.active; p.count = 0; }
  return p.active;
}
// No maximo EVENT_RATE_MAX novos eventos em qualquer janela de 60 s (backend: 30/min).
bool eventRateAllow(unsigned long now) {
  if (eventCount == EVENT_RATE_MAX && now - eventStamps[eventHead] < EVENT_RATE_WINDOW_MS) return false;
  if (eventCount < EVENT_RATE_MAX) {
    eventStamps[(eventHead + eventCount) % EVENT_RATE_MAX] = now; eventCount++;
  } else {
    eventStamps[eventHead] = now; eventHead = (eventHead + 1) % EVENT_RATE_MAX;
  }
  return true;
}
void recordFail(const char *ctx, ApiResult r) {
  snprintf(lastErrTag, sizeof(lastErrTag), "%s:%s:%d", ctx, failTag(r.fail), r.code);
  reportDirty = true;
}

// ---------------------------------------------------------------------------
// HTTPS (sincrono, limitado por timeouts) com classificacao de falhas
// ---------------------------------------------------------------------------
// Separa host e monta a URL final. Falha se API_BASE nao for HTTPS valido.
bool buildUrl(const char *path, char *host, size_t hostSz, char *url, size_t urlSz) {
  const char *base = API_BASE;
  if (strncmp(base, "https://", 8) != 0 || strstr(base, "SEU-TUNEL")) return false;
  const char *h = base + 8;
  size_t auth = strcspn(h, "/?#");
  if (auth == 0 || memchr(h, '@', auth)) return false;
  size_t n = 0;
  while (n < auth && h[n] != ':') n++;
  if (n == 0 || n >= hostSz) return false;
  memcpy(host, h, n); host[n] = 0;
  size_t bl = strlen(base);
  while (bl > 0 && base[bl - 1] == '/') bl--;
  int w = snprintf(url, urlSz, "%.*s%s", (int)bl, base, path);
  return w > 0 && (size_t)w < urlSz;
}
void logApiFail(const char *path, ApiResult r, const char *host, const char *ip,
                unsigned long ms, int tlsCode, const char *tlsMsg) {
  Serial.printf("[API] %s -> %s codigo=%d host=%s ip=%s %lums", path, failTag(r.fail), r.code,
                host[0] ? host : "-", ip[0] ? ip : "-", ms);
  if (tlsCode) {
    Serial.printf(" tls=%d", tlsCode);
    if (tlsCode < -1) Serial.printf(" (%s)", tlsMsg);
  }
  Serial.println();
}
// closed() e aplicado ANTES de toda requisicao; nunca ha atuacao nesta funcao.
ApiResult apiPost(const char *path, JsonDocument &payload, JsonDocument &result) {
  closed();
  result.clear();
  unsigned long t0 = millis();
  char host[96] = "", url[160] = "", ipStr[16] = "", tlsMsg[100] = "", body[192];
  int tlsCode = 0;
  ApiResult r = {Fail::None, 0};
  if (WiFi.status() != WL_CONNECTED) r = {Fail::Wifi, 0};
  else if (strlen(ROOT_CA) < 200 || strstr(ROOT_CA, "CONFIGURE") ||
           !strstr(ROOT_CA, "-----BEGIN CERTIFICATE-----")) r = {Fail::CfgCa, 0};
  else if (!buildUrl(path, host, sizeof(host), url, sizeof(url))) r = {Fail::CfgUrl, 0};
  else if (!strlen(DEVICE_ID) || !strlen(DEVICE_SECRET) ||
           !strncmp(DEVICE_SECRET, "SEGREDO-EXCLUSIVO", 17)) r = {Fail::CfgDevice, 0};
  else if (!timeSynced()) r = {Fail::Ntp, 0};
  else if (ESP.getMaxAllocHeap() < MIN_HEAP_FOR_TLS) r = {Fail::Mem, (int)ESP.getMaxAllocHeap()};
  if (r.fail != Fail::None) { logApiFail(path, r, host, ipStr, millis() - t0, 0, ""); return r; }

  IPAddress address;
  if (WiFi.hostByName(host, address) != 1) {
    r = {Fail::Dns, 0};
    logApiFail(path, r, host, ipStr, millis() - t0, 0, ""); return r;
  }
  snprintf(ipStr, sizeof(ipStr), "%s", address.toString().c_str());
  size_t n = measureJson(payload);
  if (n == 0 || n >= sizeof(body)) {
    r = {Fail::Json, (int)n};
    logApiFail(path, r, host, ipStr, millis() - t0, 0, ""); return r;
  }
  serializeJson(payload, body, sizeof(body));

  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS); http.setTimeout(HTTP_TIMEOUT_MS);
  // Redirecionamentos nao sao seguidos (comportamento padrao do HTTPClient).
  if (!http.begin(client, url)) {
    r = {Fail::CfgUrl, 0};
    logApiFail(path, r, host, ipStr, millis() - t0, 0, ""); return r;
  }
  const char *keys[] = {"Content-Type"};
  http.collectHeaders(keys, 1);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Secret", DEVICE_SECRET);
  int code = http.POST((uint8_t *)body, n);
  if (code <= 0) {
    tlsCode = client.lastError(tlsMsg, sizeof(tlsMsg));
    r = {classifyTransport(code, tlsCode, millis() - t0), code};
  } else {
    r = {classifyHttp(code), code};
    if (r.fail == Fail::None) {
      int size = http.getSize();
      String ct = http.header("Content-Type");
      String resp = (size > (int)RESP_MAX) ? String() : http.getString();
      bool html = ct.indexOf("html") >= 0 || (resp.length() && resp[0] == '<');
      if (size > (int)RESP_MAX || resp.length() > RESP_MAX) r = {Fail::Json, 200};
      else if (html) r = {Fail::Proxy, 200};
      else if (deserializeJson(result, resp) != DeserializationError::Ok || !result.is<JsonObject>())
        r = {Fail::Json, 200};
    }
  }
  http.end();
  if (r.fail != Fail::None) {
    result.clear();
    logApiFail(path, r, host, ipStr, millis() - t0, tlsCode, tlsMsg);
  }
  return r;
}

// ---------------------------------------------------------------------------
// Falhas, eventos pendentes e pedido de acesso
// ---------------------------------------------------------------------------
// Falha de API: cancela fechada + LED vermelho. So uma recusa explicita do backend
// no pedido de acesso (400/409/422) vira ACESSO_NEGADO; o resto e falha de rede/config.
void apiFailure(ApiResult r, bool isAccess) {
  closed();
  bool refusal = isAccess && r.fail == Fail::Rejected;
  if (!refusal) apiReady = false;
  showNet(refusal ? "ACESSO_NEGADO" : "FALHA_REDE", failLcd(r.fail));
}
void clearPending() { pendingEvent[0] = 0; pendingType[0] = 0; attempts = 0; }
void dropPending(const char *why) {
  failedEvents++;
  snprintf(lastErrTag, sizeof(lastErrTag), "evt:%s", why); reportDirty = true;
  Serial.printf("[EVT] descartado event_id=%s tipo=%s tentativas=%u motivo=%s total_descartados=%lu\n",
                pendingEvent, pendingType, attempts, why, failedEvents);
  clearPending();
  closed();
}
void accessAttempt() {
  closed();
  show("VALIDANDO");
  reqDoc.clear();
  reqDoc["device_id"] = DEVICE_ID;
  reqDoc["event_id"] = pendingEvent;
  reqDoc["reservation_id"] = reservation;
  reqDoc["type"] = pendingType;
  ApiResult r = apiPost("/api/iot/access-request", reqDoc, respDoc);
  attempts++;
  if (r.fail == Fail::None) {
    bool valid = respDoc["allowed"].is<bool>() && respDoc["actuate"].is<bool>() &&
                 respDoc["event_id"].is<const char *>() &&
                 !strcmp(respDoc["event_id"].as<const char *>(), pendingEvent);
    if (!valid) {
      r = {Fail::Json, 200};
      Serial.println("[API] /api/iot/access-request -> JSON campos de autorizacao/event_id invalidos");
    }
  }
  unsigned long now = millis();
  if (r.fail != Fail::None) {
    recordFail("access", r);
    apiFailure(r, true);
    if (failRetryable(r.fail) && attempts < ACCESS_MAX_ATTEMPTS) {
      unsigned long d = backoffDelay(attempts, ACCESS_BACKOFF_MIN_MS, ACCESS_BACKOFF_MAX_MS);
      if (r.fail == Fail::Rate && d < TELEMETRY_INTERVAL_MS) d = TELEMETRY_INTERVAL_MS;
      pendingNextAt = now + d;
      Serial.printf("[EVT] nova tentativa %u/%u em %lums (mesmo event_id)\n",
                    attempts + 1, ACCESS_MAX_ATTEMPTS, d);
    } else {
      dropPending(failTag(r.fail));
    }
    return;
  }
  apiReady = true;
  bool allowed = respDoc["allowed"].as<bool>(), actuate = respDoc["actuate"].as<bool>();
  if (allowed && actuate) {
    mover_cancela(&cancela_status, ABERTA, &cancela);
    openedAt = millis(); if (!openedAt) openedAt = 1;
    show("ACESSO_LIBERADO", "Cancela aberta");
  } else {
    closed();
    show("ACESSO_NEGADO", respDoc["duplicate"].as<bool>() ? "Evento repetido" : "Consulte reserva");
  }
  clearPending();
}
void newEvent(const char *kind) {
  if (!reservation[0]) { show("ACESSO_NEGADO", "Informe reserva"); return; }
  unsigned long now = millis();
  if (!eventRateAllow(now)) {
    Serial.println("[EVT] limite local de eventos por minuto atingido; evento ignorado");
    show("ACESSO_NEGADO", "Aguarde (limite)");
    return;
  }
  snprintf(pendingEvent, sizeof(pendingEvent), "esp-%08lx-%08lx-%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random(), now);
  snprintf(pendingType, sizeof(pendingType), "%s", kind);
  attempts = 0; pendingCreatedAt = now; pendingNextAt = now;
  Serial.printf("[EVT] novo %s event_id=%s\n", kind, pendingEvent);
  accessAttempt();
}

// ---------------------------------------------------------------------------
// Wi-Fi, NTP e telemetria (nao bloqueantes: so agendamento por millis)
// ---------------------------------------------------------------------------
// true quando conectado. Desconexao fecha a cancela e invalida a API.
bool wifiService(unsigned long now) {
  wl_status_t st = WiFi.status();
  if (st != lastWifiStatus) {
    Serial.printf("[WIFI] status=%d (%s)\n", (int)st, wifiName(st));
    lastWifiStatus = st;
  }
  if (st == WL_CONNECTED) {
    if (!wifiUp) {
      wifiUp = wifiEverUp = true;
      telemetryUrgent = true; telemetryFails = 0;
      Serial.printf("[WIFI] conectado ip=%s rssi=%ddBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    }
    return true;
  }
  if (wifiUp) {
    wifiUp = false; apiReady = false; telemetryUrgent = true;
    Serial.println("[WIFI] desconectado; cancela fechada ate nova validacao da API");
  }
  closed();
  bool booting = !wifiEverUp && now - bootAt < WIFI_BOOT_GRACE_MS;
  if (booting) showNet("INICIALIZANDO", "Conectando WiFi");
  else showNet("FALHA_REDE", failLcd(Fail::Wifi));
  if (now - lastWifi > WIFI_RECONNECT_MS) conectar_wifi(WIFI_SSID, WIFI_PASSWORD);
  return false;
}
void startNtp(unsigned long now) {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  ntpState = NTPS_WAIT; ntpMarkAt = now;
  Serial.println("[NTP] sincronizacao solicitada (pool.ntp.org, time.google.com)");
}
// true quando a hora e valida. Enquanto espera, a cancela segue fechada e nao e "falha".
bool ntpService(unsigned long now) {
  if (timeSynced()) {
    if (ntpState != NTPS_OK) {
      ntpState = NTPS_OK;
      Serial.printf("[NTP] sincronizado epoch=%lu\n", (unsigned long)time(nullptr));
      showNet("INICIALIZANDO", "Sincronizando API");
    }
    return true;
  }
  closed();
  if (ntpState == NTPS_IDLE || ntpState == NTPS_OK) startNtp(now);
  else if (ntpState == NTPS_WAIT && now - ntpMarkAt >= NTP_TIMEOUT_MS) {
    ntpState = NTPS_FAIL; ntpMarkAt = now;
    Serial.println("[NTP] sem sincronizacao no prazo; nova tentativa em 30 s");
  } else if (ntpState == NTPS_FAIL && now - ntpMarkAt >= NTP_RETRY_MS) startNtp(now);
  if (ntpState == NTPS_FAIL) showNet("FALHA_REDE", failLcd(Fail::Ntp));
  else showNet("INICIALIZANDO", "Aguard. NTP");
  return false;
}
const char *telemetryStatus() {
  return maintenance ? "MANUTENCAO" : (!apiReady ? "PRONTO" : state);
}
void telemetryService(unsigned long now) {
  if (telemetryAttempted && now - lastTelemetryAt < TELEMETRY_MIN_GAP_MS) return;
  if (telemetryAttempted && !telemetryUrgent && !reached(now, nextTelemetryAt)) return;
  reqDoc.clear();
  reqDoc["device_id"] = DEVICE_ID;
  reqDoc["status"] = telemetryStatus();
  // Campos opcionais: o Flask atual le apenas device_id e status e ignora os demais.
  if (failedEvents) reqDoc["failed_events"] = failedEvents;
  if (reportDirty && lastErrTag[0]) reqDoc["last_error"] = lastErrTag;
  ApiResult r = apiPost("/api/iot/telemetry", reqDoc, respDoc);
  if (r.fail == Fail::None && !(respDoc["received"].is<bool>() && respDoc["received"].as<bool>())) {
    r = {Fail::Json, 200};
    Serial.println("[API] /api/iot/telemetry -> JSON confirmacao ausente");
  }
  // Intervalo contado APOS a chamada: timeout nao provoca nova tentativa imediata.
  lastTelemetryAt = millis(); telemetryAttempted = true; telemetryUrgent = false;
  if (r.fail != Fail::None) {
    if (telemetryFails < 250) telemetryFails++;
    recordFail("telemetry", r);
    apiFailure(r, false);
    unsigned long d = backoffDelay(telemetryFails, TELEMETRY_BACKOFF_MIN_MS, TELEMETRY_BACKOFF_MAX_MS);
    if (r.fail == Fail::Rate && d < TELEMETRY_INTERVAL_MS) d = TELEMETRY_INTERVAL_MS;
    nextTelemetryAt = lastTelemetryAt + d;
    Serial.printf("[API] telemetria: falha %u, proxima tentativa em %lums\n", telemetryFails, d);
  } else {
    bool recovered = !apiReady;
    telemetryFails = 0; apiReady = true; reportDirty = false;
    nextTelemetryAt = lastTelemetryAt + TELEMETRY_INTERVAL_MS + (esp_random() % 1000);
    if (maintenance) show("MANUTENCAO");
    else if (recovered && !pendingEvent[0]) show("PRONTO", "API conectada");
  }
}

// ---------------------------------------------------------------------------
// Entrada Serial (codigo da reserva) sem bloqueio e com limite
// ---------------------------------------------------------------------------
void handleReservationLine(char *line) {
  while (*line == ' ' || *line == '\t') line++;
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
  bool valid = n >= 4 && n <= RES_MAX_LEN && !strncmp(line, "SP-", 3);
  for (size_t i = 3; valid && i < n; i++) {
    char c = line[i];
    valid = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_';
  }
  if (!valid) { Serial.println("[SERIAL] codigo invalido: use SP- seguido de letras, numeros, - ou _ (ate 64)."); return; }
  if (pendingEvent[0] || openedAt) { Serial.println("[SERIAL] ocupado: aguarde o evento atual terminar."); return; }
  memcpy(reservation, line, n + 1);
  Serial.printf("Reserva selecionada (%u caracteres).\n", (unsigned)n);
}
void pollSerial() {
  static char buf[RES_MAX_LEN + 2];
  static size_t len = 0;
  static bool overflow = false;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (overflow) Serial.println("[SERIAL] entrada longa demais (max 64); ignorada.");
      else if (len) { buf[len] = 0; handleReservationLine(buf); }
      len = 0; overflow = false;
      continue;
    }
    if (overflow) continue;
    if (len >= RES_MAX_LEN + 1) { overflow = true; continue; }  // descarta a linha inteira
    buf[len++] = (char)c;
  }
}

// ---------------------------------------------------------------------------
void statLog(unsigned long now) {
  if (now - lastStat < STAT_INTERVAL_MS) return;
  lastStat = now;
  Serial.printf("[STAT] up=%lus estado=%s wifi=%s rssi=%d api=%d tel_falhas=%u evt_descartados=%lu heap=%u min=%u\n",
                now / 1000, state, wifiName(WiFi.status()),
                WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0, apiReady ? 1 : 0, telemetryFails,
                failedEvents, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}
void setup() {
  Serial.begin(115200);
  init_pins();
  init_cancela(&cancela_status, PIN_SERVO, &cancela);
  lcd.init(); lcd.backlight(); show("INICIALIZANDO", "Conectando WiFi");
  bootAt = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  conectar_wifi(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[CFG] API_BASE=%s DEVICE_ID=%s ROOT_CA=%u chars SECRET=%u chars\n", API_BASE, DEVICE_ID,
                (unsigned)strlen(ROOT_CA), (unsigned)strlen(DEVICE_SECRET));
  Serial.println("Digite o codigo SP-... da reserva e Enter. Dois botoes alternam manutencao.");
}
void loop() {
  unsigned long now = millis();
  pollSerial();
  statLog(now);
  if (pendingEvent[0] && now - pendingCreatedAt >= EVENT_TTL_MS) dropPending("expirado");
  if (openedAt) {
    if (now - openedAt >= OPEN_HOLD_MS) { closed(); show("PRONTO", "Aguardando evento"); }
    delay(20); return;
  }
  // GPIO 27 = entrada, GPIO 14 = saida; os dois juntos alternam manutencao.
  bool btnIn = debounceUpdate(btnInDeb, digitalRead(PIN_BTN_2) == LOW, now);
  bool btnOut = debounceUpdate(btnOutDeb, digitalRead(PIN_BTN_1) == LOW, now);
  bool both = btnIn && btnOut;
  if (both && !bothWas) {
    maintenance = !maintenance;
    if (pendingEvent[0]) { Serial.println("[EVT] pedido pendente cancelado (manutencao)"); clearPending(); }
    closed(); apiReady = false;
    entryWas = exitWas = true;  // sem borda de subida ao voltar
    show(maintenance ? "MANUTENCAO" : "INICIALIZANDO", "Sincronizando API");
    telemetryUrgent = true;  // continua respeitando o intervalo minimo de 5 s
  }
  bothWas = both;
  if (!wifiService(now)) { delay(50); return; }
  if (!ntpService(now)) { delay(50); return; }
  telemetryService(now);
  if (maintenance) { delay(50); return; }
  // HTTP/TLS pode ter bloqueado por varios segundos: rele relogio, botoes e sensores.
  now = millis();
  btnIn = debounceUpdate(btnInDeb, digitalRead(PIN_BTN_2) == LOW, now);
  btnOut = debounceUpdate(btnOutDeb, digitalRead(PIN_BTN_1) == LOW, now);
  both = btnIn && btnOut;
  bool sensorIn = presenceUpdate(presIn, distanceCM(PIN_TRIG_HCSR04, PIN_ECHO_HCSR04));
  bool sensorOut = presenceUpdate(presOut, distanceCM(PIN_TRIG_SAIDA, PIN_ECHO_SAIDA));
  bool entering = btnIn || sensorIn, exiting = btnOut || sensorOut;
  // Entrada e saida ativas ao mesmo tempo (exceto o combo de manutencao): nao gera evento.
  bool conflict = entering && exiting && !both;
  if (conflict != conflictWas) {
    conflictWas = conflict;
    Serial.println(conflict ? "[SENS] conflito: entrada e saida ativas; eventos bloqueados"
                            : "[SENS] conflito resolvido");
    if (conflict && apiReady) show("ACESSO_NEGADO", "Conflito sensor");
    else if (!conflict && apiReady && !pendingEvent[0]) show("PRONTO");
  }
  if (pendingEvent[0]) {
    if (reached(now, pendingNextAt)) accessAttempt();
  } else if (!both && !conflict && now - statusAt > 400) {
    bool enterEdge = entering && !entryWas, exitEdge = exiting && !exitWas;
    if (enterEdge || exitEdge) {
      if (!apiReady) Serial.println("[EVT] ignorado: API indisponivel (cancela permanece fechada)");
      else newEvent(enterEdge ? "ENTRY" : "EXIT");
    }
  }
  entryWas = entering; exitWas = exiting;
  now = millis();
  if (apiReady && !pendingEvent[0] && !conflict && (!strcmp(state, "ACESSO_NEGADO") || !strcmp(state, "ACESSO_LIBERADO")) &&
      now - statusAt > 3000) show("PRONTO");
  delay(40);
}
