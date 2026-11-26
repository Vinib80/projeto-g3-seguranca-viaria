// Testes da lógica no PC (sem ESP32).
// Rodar: g++ -std=c++17 -I include -I <pasta-com-ArduinoJson.h> test/host/teste_logica.cpp -o t && ./t
#include <cstdio>
#include <cstdlib>
#include "logica.h"

static int falhas = 0;
#define CHECAR(c) do { if (!(c)) { printf("FALHOU linha %d: %s\n", __LINE__, #c); falhas++; } } while (0)

int main() {
  const char* json = R"([
    {"id":"10-julho","lat":-8.0012,"lon":-34.9301,"raio_m":80,"risco":"alto","horas":[17,18,19],"vel_segura":30},
    {"id":"sempre","lat":-8.0545,"lon":-34.8829,"raio_m":200,"risco":"medio","horas":[],"vel_segura":40},
    {"lat":1,"lon":2}
  ])";
  Trecho t[MAX_TRECHOS];
  int n = lerTrechos(json, t, MAX_TRECHOS);
  CHECAR(n == 2);                               // o terceiro não tem id
  CHECAR(strcmp(t[0].id, "10-julho") == 0);
  CHECAR(t[0].horasMask == ((1UL<<17)|(1UL<<18)|(1UL<<19)));
  CHECAR(t[0].velSegura == 30 && t[0].raioM == 80);
  CHECAR(lerTrechos("isso não é json", t, MAX_TRECHOS) == -1);
  CHECAR(lerTrechos("{\"a\":1}", t, MAX_TRECHOS) == -1);
  CHECAR(lerTrechos("[]", t, MAX_TRECHOS) == 0);
  n = lerTrechos(json, t, MAX_TRECHOS);

  // distância: 0.001 grau de latitude ~ 111 m
  double d = distanciaM(-8.0, -34.9, -8.001, -34.9);
  CHECAR(d > 110 && d < 112);

  // dentro do 10-julho às 18h
  Avaliacao a = avaliar(t, n, -8.0012, -34.9301, 25, 18);
  CHECAR(a.alerta == AMARELO && a.indice == 0);
  a = avaliar(t, n, -8.0012, -34.9301, 31, 18);
  CHECAR(a.alerta == VERMELHO && a.indice == 0);
  // fora do horário -> verde
  a = avaliar(t, n, -8.0012, -34.9301, 60, 10);
  CHECAR(a.alerta == VERDE && a.indice == -1);
  // hora desconhecida -> conta como ativo
  a = avaliar(t, n, -8.0012, -34.9301, 60, -1);
  CHECAR(a.alerta == VERMELHO);
  // 100 m de distância (raio 80) -> fora
  a = avaliar(t, n, -8.0021, -34.9301, 60, 18);
  CHECAR(a.alerta == VERDE);
  // trecho sem horas vale o dia todo
  a = avaliar(t, n, -8.0545, -34.8829, 10, 3);
  CHECAR(a.alerta == AMARELO && a.indice == 1);

  // telemetria
  char buf[200];
  size_t len = montarTelemetria(buf, sizeof(buf), 1732450000, -8.0012, -34.9301, 46.6f, "10-julho", VERMELHO);
  printf("%s\n", buf);
  CHECAR(len > 0);
  JsonDocument doc;
  CHECAR(deserializeJson(doc, buf) == DeserializationError::Ok);
  CHECAR(doc["ts"] == 1732450000 && doc["vel_kmh"] == 47);
  CHECAR(doc["trecho_id"] == "10-julho" && doc["alerta"] == "vermelho");
  CHECAR(fabs(doc["lat"].as<double>() + 8.0012) < 1e-6);
  len = montarTelemetria(buf, sizeof(buf), 0, -8.0, -34.9, 0, nullptr, VERDE);
  printf("%s\n", buf);
  deserializeJson(doc, buf);
  CHECAR(doc["trecho_id"].isNull() && doc["alerta"] == "verde");
  CHECAR(montarTelemetria(buf, 10, 0, 0, 0, 0, nullptr, VERDE) == 0);  // não cabe

  // comandos
  const char* c1 = "{\"acao\":\"silenciar\"}";
  CHECAR(lerComando(c1, strlen(c1)) == CMD_SILENCIAR);
  const char* c2 = "{\"acao\":\"atualizar_trechos\"}";
  CHECAR(lerComando(c2, strlen(c2)) == CMD_ATUALIZAR_TRECHOS);
  CHECAR(lerComando("lixo", 4) == CMD_NENHUM);

  printf(falhas ? "%d falha(s)\n" : "todos os testes passaram\n", falhas);
  return falhas ? 1 : 0;
}
