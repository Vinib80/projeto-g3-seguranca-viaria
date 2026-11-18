/*
 * Projeto 6 - Grupo 3 | Modulo veicular de alerta
 *
 * Potenciometro: GPIO4
 * Buzzer: GPIO18
 * LED verde: GPIO25
 * LED amarelo: GPIO26
 * LED vermelho: GPIO27
 */
#include <Arduino.h>

#define PIN_POT          4
#define PIN_BUZZER       18
#define PIN_LED_VERDE    25
#define PIN_LED_AMARELO  26
#define PIN_LED_VERMELHO 27

#define SERIAL_BAUD      115200
#define VELOCIDADE_MAXIMA 120.0f
#define FREQUENCIA_BUZZER 2500

void desligarTodasSaidas() {
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_AMARELO, LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

void atualizarAlertas(float velocidade) {
  desligarTodasSaidas();

  if (velocidade <= 40.0f) {
    digitalWrite(PIN_LED_VERDE, HIGH);
  } else if (velocidade <= 80.0f) {
    digitalWrite(PIN_LED_AMARELO, HIGH);
  } else {
    digitalWrite(PIN_LED_VERMELHO, HIGH);

    // Bipe ativo: 150 ms ligado e 150 ms desligado.
    bool buzzerLigado = (millis() / 150) % 2 == 0;
    if (buzzerLigado) {
      tone(PIN_BUZZER, FREQUENCIA_BUZZER);
    } else {
      noTone(PIN_BUZZER);
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_AMARELO, LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);

  Serial.println("Sistema iniciado");
  Serial.println("Velocidade | LEDs | Buzzer");
}

void loop() {
  const int quantidadeAmostras = 8;
  long soma = 0;

  for (int i = 0; i < quantidadeAmostras; i++) {
    soma += analogRead(PIN_POT);
  }

  float leituraMedia = soma / (float)quantidadeAmostras;
  float velocidade = (leituraMedia * VELOCIDADE_MAXIMA) / 4095.0f;

  atualizarAlertas(velocidade);

  Serial.printf("Velocidade: %.1f km/h\n", velocidade);
  delay(250);
}
