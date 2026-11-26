/*
 * Configuração do módulo. Mude aqui, não no main.cpp.
 *
 * Senha de Wi-Fi e IP da sua casa NÃO devem ir para o GitHub (o repositório é público).
 * Crie include/segredos.h (já está no .gitignore) copiando include/segredos.exemplo.h:
 * o que estiver lá substitui os valores abaixo.
 */
#pragma once

#if __has_include("segredos.h")
#include "segredos.h"
#endif

// ------------------------- Pinos (circuito atual) -------------------------
#define PIN_POT          34   // velocidade (ADC1, funciona com Wi-Fi ligado)
#define PIN_BUZZER       18
#define PIN_LED_VERDE    25
#define PIN_LED_AMARELO  26
#define PIN_LED_VERMELHO 27
// Reservados para depois: GPS NEO-6M em UART2 (RX 16, TX 17); CAN (SN65HVD230) em TX 21, RX 22.

// ------------------------- Rede -------------------------
#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"   // no Wokwi funciona assim; na bancada, use segredos.h
#endif
#ifndef WIFI_SENHA
#define WIFI_SENHA ""
#endif

// IP do PC onde roda o docker-compose (Mosquitto + API). Nunca "localhost": o ESP32 é outra máquina.
#ifndef SERVIDOR_IP
#define SERVIDOR_IP "broker.hivemq.com"   // broker público, só para testar sem o PC
#endif
#ifndef MQTT_PORTA
#define MQTT_PORTA 1883
#endif
#ifndef API_URL_TRECHOS
#define API_URL_TRECHOS "http://" SERVIDOR_IP ":8000/modulo/trechos"
#endif

#define MODULO_ID "01"
#define TOPICO_TELEMETRIA "cttu/modulo/" MODULO_ID "/telemetria"
#define TOPICO_COMANDO    "cttu/modulo/" MODULO_ID "/comando"
#define TOPICO_STATUS     "cttu/modulo/" MODULO_ID "/status"   // "online"/"offline" (last will)

#define INTERVALO_TELEMETRIA_MS 1000
#define INTERVALO_TRECHOS_MS    (5UL * 60UL * 1000UL)  // baixa os trechos de novo a cada 5 min

// ------------------------- Hora -------------------------
// Os trechos têm "horas" em que o risco vale. A hora vem do NTP (precisa de internet).
// Sem NTP, usa HORA_DEMO. Com -1, todo trecho conta como ativo (bom para a demo).
#ifndef HORA_DEMO
#define HORA_DEMO -1
#endif
#define FUSO_SEGUNDOS (-3 * 3600)

// ------------------------- Simulação -------------------------
#define VEL_MAXIMA 120.0f   // km/h com o potenciômetro no máximo
#define FREQUENCIA_BUZZER 2500

// Rota simulada (substitui o GPS): vai e volta entre os pontos abaixo.
#define ROTA_PERIODO_MS 60000UL
static const double ROTA[][2] = {
  {-8.0650, -34.8829},
  {-8.0506, -34.8829},
};

// Trechos de reserva: usados até a API responder (ou se ela nunca responder).
// Mesmo formato do contrato. "horas": [] = vale o dia todo.
static const char TRECHOS_PADRAO[] = R"JSON([
  {"id":"trecho-sul","lat":-8.0620,"lon":-34.8829,"raio_m":150,"risco":"alto","horas":[],"vel_segura":30},
  {"id":"trecho-norte","lat":-8.0545,"lon":-34.8829,"raio_m":200,"risco":"medio","horas":[],"vel_segura":40}
])JSON";
