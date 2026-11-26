/*
 * Lógica pura do módulo (sem pinos, sem Wi-Fi).
 * Pode ser testada no PC: veja test/host/teste_logica.cpp.
 *
 * Segue o contrato em contratos.md:
 *  - lerTrechos()       -> lê a lista de trechos que a API devolve
 *  - avaliar()          -> decide verde / amarelo / vermelho
 *  - montarTelemetria() -> monta o payload MQTT
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <ArduinoJson.h>

#define MAX_TRECHOS 32

struct Trecho {
  char     id[32];
  double   lat;
  double   lon;
  float    raioM;
  char     risco[8];    // "baixo", "medio" ou "alto"
  uint32_t horasMask;   // bit h ligado = risco vale na hora h; 0 = vale o dia todo
  float    velSegura;   // km/h
};

enum Alerta : uint8_t { VERDE = 0, AMARELO = 1, VERMELHO = 2 };

inline const char* nomeAlerta(Alerta a) {
  switch (a) {
    case AMARELO:  return "amarelo";
    case VERMELHO: return "vermelho";
    default:       return "verde";
  }
}

// Distância em metros entre dois pontos (haversine).
inline double distanciaM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  const double rad = M_PI / 180.0;
  double dLat = (lat2 - lat1) * rad;
  double dLon = (lon2 - lon1) * rad;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * rad) * cos(lat2 * rad) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R * asin(sqrt(a));
}

// Lê o JSON da API (lista de trechos). Devolve quantos trechos leu, ou -1 se o JSON for inválido.
// Trechos sem id, lat ou lon são ignorados.
inline int lerTrechos(const char* json, Trecho* saida, int max) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return -1;
  JsonArray lista = doc.as<JsonArray>();
  if (lista.isNull()) return -1;

  int n = 0;
  for (JsonObject o : lista) {
    if (n >= max) break;
    if (!o["id"].is<const char*>() || !o["lat"].is<double>() || !o["lon"].is<double>()) continue;

    Trecho& t = saida[n];
    memset(&t, 0, sizeof(t));
    strncpy(t.id, o["id"].as<const char*>(), sizeof(t.id) - 1);
    t.lat       = o["lat"].as<double>();
    t.lon       = o["lon"].as<double>();
    t.raioM     = o["raio_m"] | 100.0f;
    t.velSegura = o["vel_segura"] | 40.0f;
    strncpy(t.risco, o["risco"] | "alto", sizeof(t.risco) - 1);
    for (JsonVariant h : o["horas"].as<JsonArray>()) {
      int hora = h.as<int>();
      if (hora >= 0 && hora <= 23) t.horasMask |= (1UL << hora);
    }
    n++;
  }
  return n;
}

struct Avaliacao {
  Alerta alerta;
  int    indice;  // trecho em que o veículo está; -1 se estiver fora
};

// Regra do contrato:
//  verde    = fora de qualquer trecho ativo
//  amarelo  = dentro de um trecho ativo
//  vermelho = dentro de um trecho ativo e acima da vel_segura dele
// hora = hora local (0-23) ou -1 se ainda não souber (aí todo trecho conta como ativo).
// Se estiver em mais de um trecho, vale o pior alerta; no empate, o trecho mais próximo.
inline Avaliacao avaliar(const Trecho* trechos, int n, double lat, double lon, float vel, int hora) {
  Avaliacao r = {VERDE, -1};
  double melhorDist = 1e12;
  for (int i = 0; i < n; i++) {
    const Trecho& t = trechos[i];
    bool ativo = t.horasMask == 0 || hora < 0 || (t.horasMask & (1UL << hora));
    if (!ativo) continue;
    double d = distanciaM(lat, lon, t.lat, t.lon);
    if (d > t.raioM) continue;

    Alerta a = (vel > t.velSegura) ? VERMELHO : AMARELO;
    if (a > r.alerta || (a == r.alerta && d < melhorDist)) {
      r.alerta = a;
      r.indice = i;
      melhorDist = d;
    }
  }
  return r;
}

// Payload do tópico cttu/modulo/<id>/telemetria. trechoId = nullptr vira null no JSON.
// Devolve o tamanho escrito (0 se não coube).
inline size_t montarTelemetria(char* buf, size_t tam, uint32_t ts, double lat, double lon,
                               float vel, const char* trechoId, Alerta alerta) {
  JsonDocument doc;
  doc["ts"]  = ts;
  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["vel_kmh"] = (int)lroundf(vel);
  if (trechoId) doc["trecho_id"] = trechoId;
  else          doc["trecho_id"] = nullptr;
  doc["alerta"] = nomeAlerta(alerta);
  size_t n = serializeJson(doc, buf, tam);
  return (n >= tam) ? 0 : n;
}

enum Comando : uint8_t { CMD_NENHUM, CMD_SILENCIAR, CMD_ATIVAR_SOM, CMD_ATUALIZAR_TRECHOS };

// Payload do tópico cttu/modulo/<id>/comando, ex.: {"acao":"silenciar"}
inline Comando lerComando(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return CMD_NENHUM;
  const char* acao = doc["acao"] | "";
  if (strcmp(acao, "silenciar") == 0)         return CMD_SILENCIAR;
  if (strcmp(acao, "ativar_som") == 0)        return CMD_ATIVAR_SOM;
  if (strcmp(acao, "atualizar_trechos") == 0) return CMD_ATUALIZAR_TRECHOS;
  return CMD_NENHUM;
}
