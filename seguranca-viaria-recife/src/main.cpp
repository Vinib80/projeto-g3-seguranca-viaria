/*
 * Projeto 6 - Grupo 3 | Módulo veicular de alerta
 * ESP32 | FreeRTOS | Wi-Fi + HTTP + MQTT
 *
 * O que faz:
 *  1. Lê a velocidade (hoje: potenciômetro; depois: CAN).
 *  2. Sabe onde está (hoje: rota simulada; depois: GPS NEO-6M).
 *  3. Compara com os trechos de risco que a API manda (contratos.md, seção 1).
 *  4. Acende verde / amarelo / vermelho e bipa no vermelho.
 *  5. Publica a telemetria no MQTT a cada 1 s (contratos.md, seção 2).
 *  6. Escuta comandos do painel em cttu/modulo/01/comando (silenciar, ativar_som, atualizar_trechos).
 *
 * Configuração (Wi-Fi, IP do servidor, pinos, rota): include/config.h
 * Regras de alerta e formatos JSON: include/logica.h
 *
 * Tarefas FreeRTOS:
 *  velocidade (núcleo 1) -> estado.vel
 *  posicao    (núcleo 1) -> estado.lat / estado.lon
 *  alerta     (núcleo 1) -> estado.alerta, LEDs e buzzer
 *  rede       (núcleo 0) -> Wi-Fi, hora (NTP), trechos (HTTP), MQTT
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <time.h>

#include "config.h"
#include "logica.h"

// ------------------------- Estado compartilhado -------------------------
struct Estado {
  float  vel;
  double lat;
  double lon;
  Alerta alerta;
  char   trechoId[32];  // vazio = fora de trecho
};

static Estado estado = {};
static Trecho trechos[MAX_TRECHOS];
static int    nTrechos = 0;
static SemaphoreHandle_t mtx;  // protege estado, trechos e nTrechos

static volatile bool somLigado    = true;
static volatile bool pedirTrechos = false;

#define TRAVAR() xSemaphoreTake(mtx, portMAX_DELAY)
#define SOLTAR() xSemaphoreGive(mtx)

static Estado lerEstado() {
  TRAVAR();
  Estado c = estado;
  SOLTAR();
  return c;
}

// ------------------------- Hora -------------------------
static bool horaSincronizada() { return time(nullptr) > 1700000000; }

static int horaLocal() {
  if (!horaSincronizada()) return HORA_DEMO;
  time_t agora = time(nullptr);
  struct tm tm;
  localtime_r(&agora, &tm);
  return tm.tm_hour;
}

// ------------------------- Tarefa: velocidade -------------------------
// Depois: trocar o analogRead pela velocidade lida no CAN (PID 0x0D). O resto não muda.
static void taskVelocidade(void*) {
  const int N = 8;
  int amostras[N] = {0};
  int idx = 0;
  long soma = 0;
  for (;;) {
    int bruto = analogRead(PIN_POT);
    soma += bruto - amostras[idx];
    amostras[idx] = bruto;
    idx = (idx + 1) % N;
    float v = (soma / (float)N) * VEL_MAXIMA / 4095.0f;

    TRAVAR();
    estado.vel = v;
    SOLTAR();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ------------------------- Tarefa: posição -------------------------
// Rota simulada: vai e volta pelos pontos de ROTA (config.h).
// Depois: trocar por TinyGPSPlus lendo a UART2 (GPIO 16/17).
static void taskPosicao(void*) {
  const int nPontos = sizeof(ROTA) / sizeof(ROTA[0]);
  for (;;) {
    float fase = (millis() % ROTA_PERIODO_MS) / (float)ROTA_PERIODO_MS;
    float ida  = (fase < 0.5f) ? fase * 2.0f : (1.0f - fase) * 2.0f;  // 0 -> 1 -> 0
    float pos  = ida * (nPontos - 1);
    int   i    = min((int)pos, nPontos - 2);
    float f    = pos - i;
    double lat = ROTA[i][0] + (ROTA[i + 1][0] - ROTA[i][0]) * f;
    double lon = ROTA[i][1] + (ROTA[i + 1][1] - ROTA[i][1]) * f;

    TRAVAR();
    estado.lat = lat;
    estado.lon = lon;
    SOLTAR();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ------------------------- Tarefa: alerta -------------------------
static void taskAlerta(void*) {
  Alerta anterior = VERDE;
  uint32_t bipeCurtoAte = 0;
  bool buzzerLigado = false;

  for (;;) {
    int hora = horaLocal();

    TRAVAR();
    Avaliacao av = avaliar(trechos, nTrechos, estado.lat, estado.lon, estado.vel, hora);
    estado.alerta = av.alerta;
    if (av.indice >= 0) strncpy(estado.trechoId, trechos[av.indice].id, sizeof(estado.trechoId) - 1);
    else                estado.trechoId[0] = '\0';
    SOLTAR();

    digitalWrite(PIN_LED_VERDE,    av.alerta == VERDE);
    digitalWrite(PIN_LED_AMARELO,  av.alerta == AMARELO);
    digitalWrite(PIN_LED_VERMELHO, av.alerta == VERMELHO);

    // Bipe curto ao entrar num trecho; bipes contínuos (150 ms on / 150 ms off) no vermelho.
    if (anterior == VERDE && av.alerta != VERDE) bipeCurtoAte = millis() + 120;
    anterior = av.alerta;

    bool janela = (millis() / 150) % 2 == 0;
    bool querLigar = somLigado &&
                     ((av.alerta == VERMELHO && janela) || millis() < bipeCurtoAte);
    if (querLigar != buzzerLigado) {
      if (querLigar) tone(PIN_BUZZER, FREQUENCIA_BUZZER);
      else           noTone(PIN_BUZZER);
      buzzerLigado = querLigar;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ------------------------- Rede -------------------------
static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);

static void onMensagem(char* topico, byte* payload, unsigned int len) {
  switch (lerComando((const char*)payload, len)) {
    case CMD_SILENCIAR:         somLigado = false;   Serial.println("[CMD] som desligado"); break;
    case CMD_ATIVAR_SOM:        somLigado = true;    Serial.println("[CMD] som ligado");    break;
    case CMD_ATUALIZAR_TRECHOS: pedirTrechos = true; Serial.println("[CMD] atualizar trechos"); break;
    default: Serial.printf("[CMD] ignorado em %s\n", topico);
  }
}

// Baixa os trechos da API. Se falhar, mantém os que já tinha.
static bool baixarTrechos() {
  static Trecho novos[MAX_TRECHOS];  // static: fora da pilha da tarefa
  WiFiClient cliente;                // separado do cliente do MQTT
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(cliente, API_URL_TRECHOS)) return false;

  int codigo = http.GET();
  bool ok = false;
  if (codigo == 200) {
    String corpo = http.getString();
    int n = lerTrechos(corpo.c_str(), novos, MAX_TRECHOS);
    if (n >= 0) {
      TRAVAR();
      memcpy(trechos, novos, sizeof(Trecho) * n);
      nTrechos = n;
      SOLTAR();
      ok = true;
      Serial.printf("[API] %d trechos recebidos\n", n);
    } else {
      Serial.println("[API] JSON inválido, mantendo trechos atuais");
    }
  } else {
    Serial.printf("[API] falhou (%d), mantendo trechos atuais\n", codigo);
  }
  http.end();
  return ok;
}

static void conectarMqtt() {
  static uint32_t ultimaTentativa = 0;
  if (mqtt.connected() || millis() - ultimaTentativa < 3000) return;
  ultimaTentativa = millis();

  String id = "g3-modulo-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("[MQTT] conectando em %s:%d... ", SERVIDOR_IP, MQTT_PORTA);
  // Last will: se o módulo cair, o broker publica "offline" no tópico de status.
  if (mqtt.connect(id.c_str(), nullptr, nullptr, TOPICO_STATUS, 0, true, "offline")) {
    mqtt.publish(TOPICO_STATUS, "online", true);
    mqtt.subscribe(TOPICO_COMANDO);
    Serial.println("OK");
  } else {
    Serial.printf("falhou (rc=%d)\n", mqtt.state());
  }
}

static void taskRede(void*) {
  mqtt.setServer(SERVIDOR_IP, MQTT_PORTA);
  mqtt.setCallback(onMensagem);
  mqtt.setBufferSize(512);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  bool ntpIniciado = false;
  bool wifiAvisado = false;
  uint32_t ultimoWifi = millis();
  uint32_t ultimaTelemetria = 0;
  uint32_t ultimosTrechos = 0;
  bool trechosDaApi = false;

  for (;;) {
    bool online = WiFi.status() == WL_CONNECTED;

    if (!online) {
      wifiAvisado = false;
      if (millis() - ultimoWifi > 10000) {  // tenta de novo a cada 10 s
        ultimoWifi = millis();
        Serial.println("[WiFi] reconectando...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_SENHA);
      }
    } else {
      if (!wifiAvisado) {
        Serial.printf("[WiFi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
        wifiAvisado = true;
      }
      if (!ntpIniciado) {
        configTime(FUSO_SEGUNDOS, 0, "a.st1.ntp.br", "pool.ntp.org");
        ntpIniciado = true;
      }
      bool vencido = millis() - ultimosTrechos > (trechosDaApi ? INTERVALO_TRECHOS_MS : 15000UL);
      if (pedirTrechos || ultimosTrechos == 0 || vencido) {
        pedirTrechos = false;
        ultimosTrechos = millis();
        if (baixarTrechos()) trechosDaApi = true;
      }
      conectarMqtt();
      mqtt.loop();
    }

    // Telemetria: vai para o MQTT (se conectado) e sempre para o monitor serial.
    if (millis() - ultimaTelemetria >= INTERVALO_TELEMETRIA_MS) {
      ultimaTelemetria = millis();
      Estado e = lerEstado();
      uint32_t ts = horaSincronizada() ? (uint32_t)time(nullptr) : 0;
      char msg[200];
      if (montarTelemetria(msg, sizeof(msg), ts, e.lat, e.lon, e.vel,
                           e.trechoId[0] ? e.trechoId : nullptr, e.alerta)) {
        if (mqtt.connected()) mqtt.publish(TOPICO_TELEMETRIA, msg);
        Serial.printf("%s%s\n", msg, mqtt.connected() ? "" : "  (MQTT offline)");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ------------------------- Setup -------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Projeto 6 G3 | módulo " MODULO_ID " ===");

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);

  mtx = xSemaphoreCreateMutex();

  nTrechos = lerTrechos(TRECHOS_PADRAO, trechos, MAX_TRECHOS);
  if (nTrechos < 0) nTrechos = 0;
  Serial.printf("%d trechos de reserva carregados; API: %s\n", nTrechos, API_URL_TRECHOS);

  xTaskCreatePinnedToCore(taskVelocidade, "velocidade", 3072,  nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskPosicao,    "posicao",    3072,  nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskAlerta,     "alerta",     4096,  nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskRede,       "rede",       10240, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);  // tudo roda nas tarefas
}
