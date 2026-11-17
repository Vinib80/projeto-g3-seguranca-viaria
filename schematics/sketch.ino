/*
 * Projeto 6 - Grupo 3 | Módulo veicular de alerta (simulação no Wokwi)
 * ESP32 DevKit | FreeRTOS | Wi-Fi + MQTT
 *
 * Simulação (Wokwi):
 *  - Velocidade: potenciômetro no GPIO34 (substitui o CAN do simulador de carro)
 *  - Posição: rota simulada por código (substitui o GPS NEO-6M)
 *  - Alerta: 3 LEDs + buzzer (sem BC547 no simulador)
 *
 * Bancada (circuito real): trocar a leitura da velocidade por TWAI/CAN (GPIO5 TX, GPIO4 RX)
 * e a posição por TinyGPSPlus (UART2: GPIO16 RX, GPIO17 TX).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>

// ------------------------- Pinos -------------------------
#define PIN_POT          34   // velocidade (ADC1)
#define PIN_LED_VERDE    25
#define PIN_LED_AMARELO  26
#define PIN_LED_VERMELHO 27
#define PIN_BUZZER       23

// true  = buzzer ativo real (nível HIGH liga, via BC547 na bancada)
// false = buzzer do Wokwi (precisa de onda; geramos ~500 Hz)
#define BUZZER_ATIVO false

// ------------------------- Rede / MQTT -------------------------
const char* WIFI_SSID  = "Wokwi-GUEST";
const char* WIFI_PASS  = "";
const int   WIFI_CANAL = 6;

const char*    MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

const char* TOPIC_TELEMETRIA = "g3/veiculo/telemetria";
const char* TOPIC_ALERTA     = "g3/veiculo/alerta";
const char* TOPIC_ZONAS      = "g3/zonas/risco";   // "1" força risco, "0" libera

// ------------------------- Regras -------------------------
const float VEL_MAX       = 120.0f;  // km/h com o potenciômetro no máximo
const float LIMITE_RISCO  = 40.0f;   // km/h acima disso em zona de risco = crítico

// Zona de risco simulada (raio em torno de um ponto) - Recife
const double ZONA_LAT    = -8.0578;
const double ZONA_LON    = -34.8829;
const double ZONA_RAIO_M = 300.0;

// Rota simulada: vai e volta na mesma avenida, passando pela zona
const double ROTA_A_LAT = -8.0650;
const double ROTA_B_LAT = -8.0506;
const double ROTA_LON   = -34.8829;
const uint32_t ROTA_PERIODO_MS = 60000;  // ida e volta

// ------------------------- Estado compartilhado -------------------------
enum Nivel : uint8_t { SEGURO = 0, RISCO = 1, CRITICO = 2 };
const char* NOME_NIVEL[] = {"SEGURO", "RISCO", "CRITICO"};

struct Estado {
  float   vel;
  double  lat;
  double  lon;
  bool    emZona;
  uint8_t nivel;
};

static Estado estado = {0, ROTA_A_LAT, ROTA_LON, false, SEGURO};
static SemaphoreHandle_t mtx;
static volatile bool riscoRemoto = false;

static Estado lerEstado() {
  Estado c;
  xSemaphoreTake(mtx, portMAX_DELAY);
  c = estado;
  xSemaphoreGive(mtx);
  return c;
}

// ------------------------- Utilitário -------------------------
static double distanciaM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  const double rad = M_PI / 180.0;
  double dLat = (lat2 - lat1) * rad;
  double dLon = (lon2 - lon1) * rad;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * rad) * cos(lat2 * rad) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R * asin(sqrt(a));
}

// ------------------------- Tarefa: velocidade (potenciômetro) -------------------------
// Na bancada: substituir por leitura de quadro CAN (TWAI) com a velocidade.
static void taskVelocidade(void*) {
  const int N = 8;
  int amostras[N] = {0};
  int idx = 0;
  long soma = 0;
  for (;;) {
    int bruto = analogRead(PIN_POT);
    soma -= amostras[idx];
    amostras[idx] = bruto;
    soma += bruto;
    idx = (idx + 1) % N;
    float media = soma / (float)N;
    float v = media * VEL_MAX / 4095.0f;

    xSemaphoreTake(mtx, portMAX_DELAY);
    estado.vel = v;
    xSemaphoreGive(mtx);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ------------------------- Tarefa: posição (GPS simulado) -------------------------
// Na bancada: substituir por TinyGPSPlus lendo a UART2 (GPIO16/17).
static void taskPosicao(void*) {
  for (;;) {
    float fase = (millis() % ROTA_PERIODO_MS) / (float)ROTA_PERIODO_MS;
    float tri = (fase < 0.5f) ? fase * 2.0f : (1.0f - fase) * 2.0f;
    double lat = ROTA_A_LAT + (ROTA_B_LAT - ROTA_A_LAT) * tri;
    double lon = ROTA_LON;
    bool dentro = distanciaM(lat, lon, ZONA_LAT, ZONA_LON) <= ZONA_RAIO_M;

    xSemaphoreTake(mtx, portMAX_DELAY);
    estado.lat = lat;
    estado.lon = lon;
    estado.emZona = dentro;
    xSemaphoreGive(mtx);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ------------------------- Tarefa: alertas (LEDs + buzzer) -------------------------
static void taskAlerta(void*) {
  bool onda = false;
  for (;;) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool risco = estado.emZona || riscoRemoto;
    uint8_t nivel = !risco ? SEGURO : (estado.vel > LIMITE_RISCO ? CRITICO : RISCO);
    estado.nivel = nivel;
    xSemaphoreGive(mtx);

    digitalWrite(PIN_LED_VERDE,    nivel == SEGURO);
    digitalWrite(PIN_LED_AMARELO,  nivel == RISCO);
    digitalWrite(PIN_LED_VERMELHO, nivel == CRITICO);

    // Buzzer: bipes de 150 ms ligado / 150 ms desligado no nível crítico
    bool janelaBipe = (millis() / 150) % 2 == 0;
    if (nivel == CRITICO && janelaBipe) {
      if (BUZZER_ATIVO) {
        digitalWrite(PIN_BUZZER, HIGH);
      } else {
        onda = !onda;                 // alterna a cada 1 ms = ~500 Hz
        digitalWrite(PIN_BUZZER, onda);
      }
    } else {
      digitalWrite(PIN_BUZZER, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ------------------------- MQTT -------------------------
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static void onMensagem(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, TOPIC_ZONAS) == 0 && len > 0) {
    riscoRemoto = (payload[0] == '1');
    Serial.printf("[MQTT] risco remoto = %d\n", riscoRemoto);
  }
}

static void conectarWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("[WiFi] conectando");
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    vTaskDelay(pdMS_TO_TICKS(250));
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " falhou");
}

static void conectarMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  String id = "g3-veiculo-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[MQTT] conectando... ");
  if (mqtt.connect(id.c_str())) {
    mqtt.subscribe(TOPIC_ZONAS);
    Serial.println("OK");
  } else {
    Serial.printf("falhou (rc=%d)\n", mqtt.state());
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static void taskMqtt(void*) {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMensagem);
  uint32_t ultimoEnvio = 0;
  int8_t ultimoNivel = -1;

  for (;;) {
    conectarWifi();
    conectarMqtt();
    mqtt.loop();

    Estado e = lerEstado();

    // Alerta: publica só quando o nível muda
    if (mqtt.connected() && e.nivel != ultimoNivel) {
      mqtt.publish(TOPIC_ALERTA, NOME_NIVEL[e.nivel], true);
      ultimoNivel = e.nivel;
    }

    // Telemetria a cada 1 s
    if (millis() - ultimoEnvio >= 1000) {
      ultimoEnvio = millis();
      char msg[160];
      snprintf(msg, sizeof(msg),
               "{\"vel\":%.1f,\"lat\":%.6f,\"lon\":%.6f,\"zona\":%d,\"alerta\":\"%s\",\"t\":%lu}",
               e.vel, e.lat, e.lon, e.emZona || riscoRemoto, NOME_NIVEL[e.nivel], millis() / 1000);
      if (mqtt.connected()) mqtt.publish(TOPIC_TELEMETRIA, msg);
      Serial.println(msg);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ------------------------- Setup -------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);

  mtx = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(taskVelocidade, "velocidade", 3072, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskPosicao,    "posicao",    3072, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskAlerta,     "alerta",     3072, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskMqtt,       "mqtt",       8192, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);  // tudo roda nas tarefas FreeRTOS
}