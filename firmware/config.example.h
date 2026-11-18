#pragma once
// Copie para config.h local. Nunca inclua tokens do Firebase, MP ou Telegram.
const char* API_BASE = "https://SEU-TUNEL";
const char* DEVICE_ID = "esp32-centro-01";
const char* DEVICE_SECRET = "SEGREDO-EXCLUSIVO-DESTE-DISPOSITIVO";
// Cole o certificado RAIZ PEM da autoridade do dominio HTTPS (nao a chave privada).
// Sem certificado valido a simulacao permanece fechada. Nao usar setInsecure().
const char* ROOT_CA = R"PEM(
-----BEGIN CERTIFICATE-----
CONFIGURE-O-CERTIFICADO-RAIZ
-----END CERTIFICATE-----
)PEM";
