# Contrato de comunicação do MVP

## 1. API → módulo (HTTP)
`GET http://<ip-do-pc>:8000/modulo/trechos` devolve uma lista JSON:

```json
[
  {
    "id": "10-julho",
    "lat": -8.0012,
    "lon": -34.9301,
    "raio_m": 80,
    "risco": "alto",
    "horas": [17, 18, 19],
    "vel_segura": 30
  }
]
```

| Campo | Tipo | Significado |
|---|---|---|
| id | string | nome do trecho, sem espaços |
| lat, lon | número | centro do trecho, graus decimais |
| raio_m | inteiro | raio de ação em metros |
| risco | string | "baixo", "medio" ou "alto" |
| horas | lista de inteiros | horas do dia em que o risco vale (0–23) |
| vel_segura | inteiro | km/h; acima disso, e dentro do trecho, o alerta é vermelho |

Regra: o dia da semana não entra no MVP. A API entrega só o que está ativo.

## 2. Módulo → painel (MQTT)
- Broker: `<ip-do-pc>`, porta 1883 (módulo) e 9001 (WebSocket, painel)
- Tópico: `cttu/modulo/01/telemetria`, a cada 1 s, QoS 0

```json
{
  "ts": 1732450000,
  "lat": -8.0012,
  "lon": -34.9301,
  "vel_kmh": 47,
  "trecho_id": "10-julho",
  "alerta": "vermelho"
}
```

| Campo | Tipo | Significado |
|---|---|---|
| ts | inteiro | segundos desde 1970 (UTC) |
| vel_kmh | inteiro | velocidade atual |
| trecho_id | string ou null | trecho em que o veículo está; null se estiver fora |
| alerta | string | "verde" (fora), "amarelo" (dentro do trecho), "vermelho" (dentro e acima de vel_segura) |

## Perguntas em aberto
- IP e portas do broker e da API (quem sobe o Docker?)
- Broker aceita conexão sem usuário e senha?