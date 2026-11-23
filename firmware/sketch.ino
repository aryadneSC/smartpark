
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <esp_system.h>
#include <Wire.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"


// PINOS
#define PIN_SERVO 18
#define PIN_ECHO_HCSR04 34
#define PIN_TRIG_HCSR04 33
#define PIN_TRIG_SAIDA 25
#define PIN_ECHO_SAIDA 26
#define PIN_LED_VERM 4
#define PIN_LED_VERD 5
#define PIN_BTN_1 14
#define PIN_BTN_2 27
#define PIN_LCD_SDA 21
#define PIN_LCD_SCL 22

// CONFIGURACOES DA SIMULACAO
#define WIFI_SSID             "Wokwi-GUEST"
#define WIFI_PASSWORD         ""
#define ANGULO_CANCELA_ABERTA 90
#define ANGULO_CANCELA_FECHADA 0
#define RESERVE_ID_BUFFER_SIZE 101
#define EVENT_ID_BUFFER_SIZE 21
#define INPUT_TIMEOUT_MS      300000UL
#define TELEMETRIA_MS         15000UL
#define SENSOR_INTERVALO_MS   300UL
#define SENSOR_PERTO_CM       25.0f
#define SENSOR_LIBERA_CM      32.0f
#define CANCELA_ABERTA_MS     3000UL
#define FEEDBACK_MS           2500UL
#define ACESSO_TIMEOUT_MS     50000UL
#define HTTP_CONNECT_MS      5000
#define HTTP_READ_MS         6000
#define TLS_HANDSHAKE_TIMEOUT_S 10UL
#define JSON_RESPONSE_MAX     1024
#define HORA_MINIMA_VALIDA    1700000000L
#define NTP_TIMEOUT_MS        30000UL

// ENUMS
typedef enum {
    INICIALIZANDO,
    PRONTO,
    VALIDANDO,
    ACESSO_LIBERADO,
    ACESSO_NEGADO,
    FALHA_REDE,
    MANUTENCAO
} device_status_t;

typedef enum { ENTRY, EXIT } type_request_t;
typedef enum { CANCELA_ABERTA, CANCELA_FECHADA } servo_status_t;
typedef enum { HTTP_ACESSO, HTTP_TELEMETRIA } http_type_t;

// STRUCTS
typedef struct {
    const char *device;
    device_status_t status;
} device_t;

typedef struct {
    char event_id[EVENT_ID_BUFFER_SIZE];
    char reservation_id[RESERVE_ID_BUFFER_SIZE];
    const char *type;
} access_request_t;

typedef struct {
    unsigned long last_debounce;
    int pin;
    int button_state;
    int debounce_delay;
    int last_button_state;
} button_t;

typedef struct {
    int pin;
    int amplitude_fechado;
    int amplitude_aberto;
    servo_status_t servo_status;
} servo_t;

typedef struct {
    http_type_t type;
    access_request_t access;
    device_status_t status;
    unsigned long enfileirado_em;
} http_request_t;

typedef struct {
    http_type_t type;
    int http_code;
    bool json_valido;
    bool allowed;
    bool actuate;
    bool duplicate;
    bool received;
    char event_id[EVENT_ID_BUFFER_SIZE];
    char reason[128];
} http_response_t;

// PROTOTIPOS
void wifi_desconectado_msg(WiFiEvent_t event, WiFiEventInfo_t info);
void ip_msg(WiFiEvent_t event, WiFiEventInfo_t info);
void gerar_event_id(char *event_id, size_t tamanho);
bool read_button(button_t *button);
void cancela_init(servo_t *cancela, Servo *servo);
void abrir_cancela(servo_t *cancela, Servo *servo);
void fechar_cancela(servo_t *cancela, Servo *servo);
void escrever_status_lcd(LiquidCrystal_I2C *lcd, device_status_t status);
void led_status(device_status_t status);
void alterar_status_device(device_status_t status, device_t *device, LiquidCrystal_I2C *lcd);
int reserve_id_request(access_request_t *access_request, type_request_t type_request, const char *reserva);
const char *type_request_to_string(type_request_t type_request);
const char *device_status_to_string(device_status_t status);
void task_http(void *param);
void iniciar_identificacao(type_request_t type);
void serial_service(void);
void wifi_service(void);
void http_result_service(void);
void sensores_service(bool *entrada, bool *saida);
void cancela_service(void);
void telemetria_service(void);
void terminar_identificacao(void);

// VARIAVEIS GLOBAIS
button_t entry_button = {};
button_t exit_button = {};
device_t device_struct = {};
Servo servo;
servo_t cancela = {};
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiClientSecure client;
QueueHandle_t queue_wifi = NULL;
QueueHandle_t queue_http = NULL;
QueueHandle_t queue_http_result = NULL;

bool ntp_iniciado = false;
bool ntp_sincronizado = false;
bool ntp_timeout = false;
unsigned long inicio_ntp = 0;
bool aguardando_codigo = false;
bool acesso_pendente = false;
bool telemetria_pendente = false;
type_request_t tipo_pendente = ENTRY;
unsigned long inicio_codigo = 0;
unsigned long inicio_acesso = 0;
unsigned long abertura_em = 0;
unsigned long feedback_em = 0;
unsigned long ultima_telemetria = 0;
unsigned long ultima_leitura_sensor = 0;
char evento_pendente[EVENT_ID_BUFFER_SIZE] = {};

char linha_serial[RESERVE_ID_BUFFER_SIZE] = {};
size_t pos_serial = 0;
bool descartar_linha = false;

bool entrada_anterior = false;
bool saida_anterior = false;

void setup() {
    Serial.begin(115200);

    pinMode(PIN_BTN_1, INPUT_PULLUP);
    pinMode(PIN_BTN_2, INPUT_PULLUP);
    pinMode(PIN_LED_VERM, OUTPUT);
    pinMode(PIN_LED_VERD, OUTPUT);
    pinMode(PIN_TRIG_HCSR04, OUTPUT);
    pinMode(PIN_ECHO_HCSR04, INPUT);
    pinMode(PIN_TRIG_SAIDA, OUTPUT);
    pinMode(PIN_ECHO_SAIDA, INPUT);
    digitalWrite(PIN_TRIG_HCSR04, LOW);
    digitalWrite(PIN_TRIG_SAIDA, LOW);

    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    lcd.init();
    lcd.backlight();

    device_struct.device = DEVICE_ID;
    device_struct.status = INICIALIZANDO;
    escrever_status_lcd(&lcd, device_struct.status);
    led_status(device_struct.status);

    entry_button = (button_t){0, PIN_BTN_1, HIGH, 50, HIGH};
    exit_button  = (button_t){0, PIN_BTN_2, HIGH, 50, HIGH};
    cancela = (servo_t){PIN_SERVO, ANGULO_CANCELA_FECHADA,
                         ANGULO_CANCELA_ABERTA, CANCELA_FECHADA};
    cancela_init(&cancela, &servo);

    queue_wifi = xQueueCreate(1, sizeof(device_status_t));
    queue_http = xQueueCreate(4, sizeof(http_request_t));
    queue_http_result = xQueueCreate(4, sizeof(http_response_t));
    if (!queue_wifi || !queue_http || !queue_http_result) {
        Serial.println("Erro ao criar filas FreeRTOS.");
        alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        for (;;) delay(1000);
    }

    client.setCACert(ROOT_CA);
    client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
    if (xTaskCreate(task_http, "HTTP", 12288, NULL, 1, NULL) != pdPASS) {
        Serial.println("Erro ao criar task HTTP.");
        alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        for (;;) delay(1000);
    }

    WiFi.mode(WIFI_STA);
    WiFi.onEvent(ip_msg, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(wifi_desconectado_msg, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(true);
    Serial.println("Conectando ao Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Comandos: !manut e !retomar");
}

void loop() {
    wifi_service();
    serial_service();           
    http_result_service();
    cancela_service();

    bool btn_entrada = read_button(&entry_button);
    bool btn_saida   = read_button(&exit_button);
    bool sensor_entrada = false, sensor_saida = false;
    sensores_service(&sensor_entrada, &sensor_saida);

    if (device_struct.status == PRONTO &&
        !aguardando_codigo && !acesso_pendente &&
        cancela.servo_status == CANCELA_FECHADA) {
        if ((btn_entrada || sensor_entrada) && !(btn_saida || sensor_saida)) {
            iniciar_identificacao(ENTRY);
        } else if ((btn_saida || sensor_saida) && !(btn_entrada || sensor_entrada)) {
            iniciar_identificacao(EXIT);
        }
    }

    telemetria_service();
    delay(1);
}

void wifi_desconectado_msg(WiFiEvent_t, WiFiEventInfo_t) {
    device_status_t status = FALHA_REDE;
    xQueueOverwrite(queue_wifi, &status);
}

void ip_msg(WiFiEvent_t, WiFiEventInfo_t) {
    device_status_t status = PRONTO;
    xQueueOverwrite(queue_wifi, &status);
}

void wifi_service(void) {
    device_status_t status;
    if (xQueueReceive(queue_wifi, &status, 0) == pdTRUE) {
        if (status == FALHA_REDE) {
            Serial.println("Wi-Fi desconectado; reconexao automatica ativa.");
            acesso_pendente = false;
            terminar_identificacao();
            fechar_cancela(&cancela, &servo);
            if (device_struct.status != MANUTENCAO)
                alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        } else {
            Serial.print("IP obtido: ");
            Serial.println(WiFi.localIP());
            if (!ntp_iniciado) {
                configTime(0, 0, "pool.ntp.org", "time.google.com");
                inicio_ntp = millis();
                ntp_iniciado = true;
            }
        }
    }
    if (ntp_iniciado && !ntp_sincronizado && WiFi.status() == WL_CONNECTED) {
        if (time(NULL) >= HORA_MINIMA_VALIDA) {
            ntp_sincronizado = true;
            Serial.println("Relogio pronto para HTTPS.");
            if (device_struct.status == INICIALIZANDO || device_struct.status == FALHA_REDE)
                alterar_status_device(PRONTO, &device_struct, &lcd);
        } else if (!ntp_timeout && millis() - inicio_ntp >= NTP_TIMEOUT_MS) {
            ntp_timeout = true;
            Serial.println("Timeout do NTP; aguardando sincronizacao do relogio.");
            if (device_struct.status != MANUTENCAO)
                alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        }
    }
    if (WiFi.status() != WL_CONNECTED &&
        device_struct.status != INICIALIZANDO &&
        device_struct.status != MANUTENCAO &&
        device_struct.status != FALHA_REDE) {
        acesso_pendente = false;
        terminar_identificacao();
        fechar_cancela(&cancela, &servo);
        alterar_status_device(FALHA_REDE, &device_struct, &lcd);
    }
}

void task_http(void *) {
    http_request_t req;

    for (;;) {
        if (xQueueReceive(queue_http, &req, portMAX_DELAY) != pdTRUE)
            continue;

        const char *operacao = req.type == HTTP_ACESSO ? "ACESSO" : "TELEMETRIA";
        const char *evento = req.type == HTTP_ACESSO ? req.access.event_id : "-";
        Serial.printf("[HTTP] Inicio %s evento=%s fila=%lu ms WiFi=%d Unix=%lld\n",
                      operacao, evento, millis() - req.enfileirado_em,
                      (int)WiFi.status(), (long long)time(NULL));

        http_response_t result = {};
        result.type = req.type;
        result.http_code = -1;

        if (req.type == HTTP_ACESSO)
            snprintf(result.event_id, sizeof(result.event_id), "%s", req.access.event_id);

        if (req.type == HTTP_ACESSO && millis() - req.enfileirado_em >= ACESSO_TIMEOUT_MS) {
            snprintf(result.reason, sizeof(result.reason), "Pedido expirou na fila HTTP");
            Serial.printf("[HTTP] Nao enviado: evento=%s %s\n", evento, result.reason);
            xQueueSend(queue_http_result, &result, portMAX_DELAY);
            continue;
        }

        if (WiFi.status() != WL_CONNECTED || time(NULL) < HORA_MINIMA_VALIDA) {
            snprintf(result.reason, sizeof(result.reason), "Wi-Fi ou horario indisponivel");
            Serial.printf("[HTTP] Nao enviado: %s\n", result.reason);
            xQueueSend(queue_http_result, &result, portMAX_DELAY);
            continue;
        }

        const String &url = req.type == HTTP_ACESSO
            ? ENDPOINT_ACESSO : ENDPOINT_TELEMETRIA;

        JsonDocument json_envio;
        json_envio["device_id"] = DEVICE_ID;

        if (req.type == HTTP_ACESSO) {
            json_envio["event_id"] = req.access.event_id;
            json_envio["reservation_id"] = req.access.reservation_id;
            json_envio["type"] = req.access.type;
        } else {
            json_envio["status"] = device_status_to_string(req.status);
        }

        String corpo;
        serializeJson(json_envio, corpo);

        HTTPClient http;
        http.setConnectTimeout(HTTP_CONNECT_MS);
        http.setTimeout(HTTP_READ_MS);

        if (!http.begin(client, url)) {
            snprintf(result.reason, sizeof(result.reason), "Falha em http.begin");
            Serial.printf("[HTTP] Nao enviado: %s\n", result.reason);
            xQueueSend(queue_http_result, &result, portMAX_DELAY);
            continue;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Device-Secret", DEVICE_SECRET);
        Serial.println(corpo);
        unsigned long inicio_post = millis();
        result.http_code = http.POST(corpo);
        unsigned long duracao_post = millis() - inicio_post;

        if (result.http_code <= 0) {
            char erro_tls[160] = {};
            int codigo_tls = client.lastError(erro_tls, sizeof(erro_tls));
            String erro_http = HTTPClient::errorToString(result.http_code);
            snprintf(result.reason, sizeof(result.reason), "%s", erro_http.c_str());
            Serial.printf("[HTTP] Erro: %s\n", erro_http.c_str());
            Serial.printf("[TLS] Ultimo erro=%d detalhe=%s\n", codigo_tls, erro_tls);
        }

        Serial.printf("[HTTP] POST %s evento=%s HTTP=%d duracao=%lu ms\n",
                      operacao, evento, result.http_code, duracao_post);

        if (result.http_code > 0) {
            int tamanho_resposta = http.getSize();

            if (tamanho_resposta > JSON_RESPONSE_MAX) {
                snprintf(result.reason, sizeof(result.reason), "Resposta muito longa");
            } else {
                String texto = http.getString();

                if (texto.length() > JSON_RESPONSE_MAX) {
                    snprintf(result.reason, sizeof(result.reason), "Resposta muito longa");
                } else {
                    JsonDocument json;
                    DeserializationError erro = deserializeJson(json, texto);

                    if (!erro) {
                        if (result.http_code == 200 && req.type == HTTP_ACESSO &&
                            json["allowed"].is<bool>() && json["actuate"].is<bool>() &&
                            json["event_id"].is<const char *>()) {

                            const char *recebido = json["event_id"].as<const char *>();
                            if (strcmp(recebido, req.access.event_id) == 0) {
                                result.json_valido = true;
                                result.allowed = json["allowed"].as<bool>();
                                result.actuate = json["actuate"].as<bool>();
                                result.duplicate = json["duplicate"].is<bool>()
                                    ? json["duplicate"].as<bool>() : false;
                            }

                        } else if (result.http_code == 200 && req.type == HTTP_TELEMETRIA &&
                                   json["received"].is<bool>()) {
                            result.json_valido = true;
                            result.received = json["received"].as<bool>();
                        }

                        const char *mensagem = result.http_code == 200
                            ? json["reason"].as<const char *>()
                            : json["error"].as<const char *>();
                        if (mensagem)
                            snprintf(result.reason, sizeof(result.reason), "%s", mensagem);
                    } else {
                        snprintf(result.reason, sizeof(result.reason), "JSON de resposta invalido");
                    }
                }
            }
        }

        http.end();
        xQueueSend(queue_http_result, &result, portMAX_DELAY);
    }
}

void http_result_service(void) {
    http_response_t res;
    while (xQueueReceive(queue_http_result, &res, 0) == pdTRUE) {
        if (res.type == HTTP_TELEMETRIA) {
            telemetria_pendente = false;
            if (res.http_code == 200 && res.json_valido && res.received) {
                if (device_struct.status == FALHA_REDE && WiFi.status() == WL_CONNECTED)
                    alterar_status_device(PRONTO, &device_struct, &lcd);
            } else {
                Serial.printf("Telemetria falhou: HTTP=%d %s\n", res.http_code, res.reason);
                if (device_struct.status != MANUTENCAO &&
                    device_struct.status != INICIALIZANDO &&
                    device_struct.status != VALIDANDO &&
                    device_struct.status != ACESSO_LIBERADO &&
                    device_struct.status != ACESSO_NEGADO) {
                    alterar_status_device(FALHA_REDE, &device_struct, &lcd);
                }
            }
            continue;
        }

        if (!acesso_pendente || strcmp(res.event_id, evento_pendente) != 0 ||
            millis() - inicio_acesso >= ACESSO_TIMEOUT_MS) {
            Serial.printf("[HTTP] Resposta de acesso ignorada: evento=%s HTTP=%d "
                          "allowed=%d actuate=%d duplicate=%d motivo=%s "
                          "(pedido ausente, expirado ou evento diferente)\n",
                          res.event_id, res.http_code, res.allowed, res.actuate,
                          res.duplicate, res.reason);
            continue;
        }
        acesso_pendente = false;

        Serial.printf("Acesso: HTTP=%d allowed=%d actuate=%d duplicate=%d %s\n",
                      res.http_code, res.allowed, res.actuate, res.duplicate, res.reason);
        if (device_struct.status == MANUTENCAO || WiFi.status() != WL_CONNECTED) continue;

        if (res.http_code == 200 && res.json_valido && res.allowed &&
            res.actuate && !res.duplicate) {
            alterar_status_device(ACESSO_LIBERADO, &device_struct, &lcd);
            abrir_cancela(&cancela, &servo);
            abertura_em = millis();
        } else if (res.http_code <= 0 || res.http_code >= 500 ||
                   (res.http_code == 200 && !res.json_valido)) {
            fechar_cancela(&cancela, &servo);
            alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        } else {
            fechar_cancela(&cancela, &servo);
            alterar_status_device(ACESSO_NEGADO, &device_struct, &lcd);
            feedback_em = millis();
        }
    }
}

void telemetria_service(void) {
    unsigned long agora = millis();
    if (WiFi.status() != WL_CONNECTED || time(NULL) < HORA_MINIMA_VALIDA) return;
    if (agora - ultima_telemetria < TELEMETRIA_MS) return;
    if (telemetria_pendente || aguardando_codigo || acesso_pendente) return;
    if (device_struct.status == INICIALIZANDO || device_struct.status == VALIDANDO) return;

    http_request_t req = {};
    req.type = HTTP_TELEMETRIA;
    req.status = device_struct.status;
    req.enfileirado_em = agora;
    if (xQueueSend(queue_http, &req, 0) == pdTRUE) {
        telemetria_pendente = true;
        ultima_telemetria = agora;
    }
}

void gerar_event_id(char *event_id, size_t tamanho) {
    uint32_t a = esp_random();
    uint32_t b = esp_random();
    snprintf(event_id, tamanho, "evt-%08lx%08lx", (unsigned long)a, (unsigned long)b);
}

const char *type_request_to_string(type_request_t type_request) {
    return type_request == ENTRY ? "ENTRY" : "EXIT";
}

const char *device_status_to_string(device_status_t status) {
    switch (status) {
        case INICIALIZANDO:
            return "INICIALIZANDO";

        case PRONTO:
            return "PRONTO";

        case VALIDANDO:
            return "VALIDANDO";

        case ACESSO_LIBERADO:
            return "ACESSO_LIBERADO";

        case ACESSO_NEGADO:
            return "ACESSO_NEGADO";

        case FALHA_REDE:
            return "FALHA_REDE";

        case MANUTENCAO:
            return "MANUTENCAO";

        default:
            return "FALHA_REDE";
    }
}

int reserve_id_request(access_request_t *request, type_request_t type, const char *reserva) {
    size_t n = strlen(reserva);
    if (n == 0 || n >= sizeof(request->reservation_id))
         return 1;

    for (size_t i = 0; i < n; ++i) {
        char c = reserva[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 1;
    }

    gerar_event_id(request->event_id, sizeof(request->event_id));
    snprintf(request->reservation_id, sizeof(request->reservation_id), "%s", reserva);
    request->type = type_request_to_string(type);

    return 0;
}

void iniciar_identificacao(type_request_t type) {
    if (WiFi.status() != WL_CONNECTED || time(NULL) < HORA_MINIMA_VALIDA) {
        alterar_status_device(FALHA_REDE, &device_struct, &lcd);
        return;
    }
    aguardando_codigo = true;
    tipo_pendente = type;
    inicio_codigo = millis();
    pos_serial = 0;
    descartar_linha = false;
    Serial.printf("Digite o ID de reserva (SP-...) para %s e pressione Enter:\n",
    type_request_to_string(type));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(type == ENTRY ? "Entrada" : "Saida");
    lcd.setCursor(0, 1);
    lcd.print("ID no Serial");
}

void terminar_identificacao(void) {
    aguardando_codigo = false;
    pos_serial = 0;
    descartar_linha = false;
}

static void comando_manutencao(bool ativar) {
    terminar_identificacao();
    acesso_pendente = false;
    fechar_cancela(&cancela, &servo);
    if (ativar) {
        alterar_status_device(MANUTENCAO, &device_struct, &lcd);
    } else {
        device_status_t st = WiFi.status() == WL_CONNECTED && time(NULL) >= HORA_MINIMA_VALIDA
            ? PRONTO : FALHA_REDE;
        alterar_status_device(st, &device_struct, &lcd);
    }
    Serial.println(ativar ? "Manutencao ativada." : "Manutencao encerrada.");
}

void serial_service(void) {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        if (c == '\r')
           continue;

        if (c != '\n') {
            if (!descartar_linha) {
                if (pos_serial < sizeof(linha_serial) - 1)
                    linha_serial[pos_serial++] = c;
                else
                    descartar_linha = true;
            }
            continue;
        }

        linha_serial[pos_serial] = '\0';
        bool linha_grande = descartar_linha;
        pos_serial = 0;
        descartar_linha = false;

        if (strcmp(linha_serial, "!manut") == 0) {
            comando_manutencao(true);
            continue;
        }
        if (strcmp(linha_serial, "!retomar") == 0) {
            comando_manutencao(false);
            continue;
        }
        if (linha_grande) {
            Serial.println("Linha longa demais.");
            continue;
        }
        if (!aguardando_codigo) {
            if (linha_serial[0]) Serial.println("Acione um botao ou sensor antes de digitar a reserva.");
            continue;
        }

        access_request_t acesso = {};
        if (reserve_id_request(&acesso, tipo_pendente, linha_serial) != 0) {
            Serial.println("ID invalido; tente novamente.");
            continue;
        }

        http_request_t req = {};
        req.type = HTTP_ACESSO;
        req.access = acesso;
        req.enfileirado_em = millis();
        if (xQueueSendToFront(queue_http, &req, 0) != pdTRUE) {
            Serial.println("Fila HTTP cheia. Tente novamente.");
            continue;
        }

        snprintf(evento_pendente, sizeof(evento_pendente), "%s", acesso.event_id);
        acesso_pendente = true;
        inicio_acesso = req.enfileirado_em;
        terminar_identificacao();
        alterar_status_device(VALIDANDO, &device_struct, &lcd);
        Serial.printf("Solicitando %s, evento %s\n",
                      acesso.type, acesso.event_id);
    }

    if (aguardando_codigo && millis() - inicio_codigo >= INPUT_TIMEOUT_MS) {
        Serial.println("Tempo esgotado para digitar reserva.");
        terminar_identificacao();
        if (device_struct.status == PRONTO)
            escrever_status_lcd(&lcd, device_struct.status);
    }
}

bool read_button(button_t *button) {
    unsigned long agora = millis();
    int leitura = digitalRead(button->pin);
    if (leitura != button->last_button_state)
        button->last_debounce = agora;
    bool apertou = false;
    if (agora - button->last_debounce >= (unsigned long)button->debounce_delay &&
        leitura != button->button_state) {
        button->button_state = leitura;
        apertou = leitura == LOW;
    }
    button->last_button_state = leitura;
    return apertou;
}

static bool presenca_hcsr04(int trig, int echo, bool anterior) {
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    unsigned long micros = pulseIn(echo, HIGH, 25000UL);
    if (micros == 0) return false;
    float distancia = micros * 0.0343f / 2.0f;
    return distancia < (anterior ? SENSOR_LIBERA_CM : SENSOR_PERTO_CM);
}

void sensores_service(bool *entrada, bool *saida) {
    *entrada = false;
    *saida = false;
    unsigned long agora = millis();
    if (agora - ultima_leitura_sensor < SENSOR_INTERVALO_MS) return;
    ultima_leitura_sensor = agora;

    bool e = presenca_hcsr04(PIN_TRIG_HCSR04, PIN_ECHO_HCSR04, entrada_anterior);
    bool s = presenca_hcsr04(PIN_TRIG_SAIDA, PIN_ECHO_SAIDA, saida_anterior);
    *entrada = e && !entrada_anterior;
    *saida = s && !saida_anterior;
    entrada_anterior = e;
    saida_anterior = s;
}

void cancela_init(servo_t *c, Servo *motor) {
    motor->attach(c->pin);
    motor->write(c->amplitude_fechado);
    c->servo_status = CANCELA_FECHADA;
}

void abrir_cancela(servo_t *c, Servo *motor) {
    motor->write(c->amplitude_aberto);
    c->servo_status = CANCELA_ABERTA;
}

void fechar_cancela(servo_t *c, Servo *motor) {
    if (c->servo_status != CANCELA_FECHADA)
        motor->write(c->amplitude_fechado);
    c->servo_status = CANCELA_FECHADA;
}

void cancela_service(void) {
    unsigned long agora = millis();
    if (acesso_pendente && agora - inicio_acesso >= ACESSO_TIMEOUT_MS) {
        acesso_pendente = false;
        Serial.printf("Timeout da autorizacao. Cancela fechada. evento=%s espera=%lu ms\n",
                      evento_pendente, agora - inicio_acesso);
        fechar_cancela(&cancela, &servo);
        if (device_struct.status != MANUTENCAO)
            alterar_status_device(FALHA_REDE, &device_struct, &lcd);
    }
    if (cancela.servo_status == CANCELA_ABERTA && agora - abertura_em >= CANCELA_ABERTA_MS) {
        fechar_cancela(&cancela, &servo);
        if (device_struct.status == ACESSO_LIBERADO)
            alterar_status_device(PRONTO, &device_struct, &lcd);
    }
    if (device_struct.status == ACESSO_NEGADO && agora - feedback_em >= FEEDBACK_MS) {
        alterar_status_device(WiFi.status() == WL_CONNECTED ? PRONTO : FALHA_REDE, &device_struct, &lcd);
    }
}

void escrever_status_lcd(LiquidCrystal_I2C *display, device_status_t status) {
    const char *msg;
    switch (status) {
        case INICIALIZANDO:
            msg = "Inicializando...";
            break;
        case PRONTO:
            msg = "Pronto!";
            break;
        case VALIDANDO:
            msg = "Validando...";
            break;
        case ACESSO_LIBERADO:
            msg = "Acesso liberado";
            break;
        case ACESSO_NEGADO:
            msg = "Acesso negado";
            break;
        case FALHA_REDE:
            msg = "Falha de rede";
            break;
        case MANUTENCAO:
            msg = "Em manutencao";
            break;
        default:
            msg = "Estado invalido";
            break;
    }

    display->clear();
    display->setCursor(0, 0);
    display->print("SMARTPARK");
    display->setCursor(0, 1);
    display->print(msg);
}

void led_status(device_status_t status) {
    digitalWrite(PIN_LED_VERD, status == ACESSO_LIBERADO ? HIGH : LOW);
    digitalWrite(PIN_LED_VERM, status == ACESSO_LIBERADO ? LOW : HIGH);
}

void alterar_status_device(device_status_t status, device_t *dev, LiquidCrystal_I2C *display) {
    if (dev->status == status) return;
    dev->status = status;
    escrever_status_lcd(display, status);
    led_status(status);
}
