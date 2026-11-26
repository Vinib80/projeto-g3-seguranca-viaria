# Infra local: broker MQTT + API mock

Para testar o módulo sem depender do ambiente dos outros. Atende ao requisito
"Mosquitto no próprio PC (contêiner)".

## 1. Subir

```bash
cd infra
docker compose up
```

- Mosquitto: porta **1883** (ESP32) e **9001** (WebSocket, para o painel web)
- API mock: http://localhost:8000/modulo/trechos (devolve `api-mock/trechos.json`)

Sem Docker: instale o Mosquitto, rode `mosquitto -c mosquitto/mosquitto.conf`, e em
outro terminal `cd api-mock && pip install -r requirements.txt && uvicorn app:app --host 0.0.0.0 --port 8000`.

## 2. Apontar o ESP32 para o seu PC

1. Descubra o IP do PC na rede: `ipconfig` (Windows) ou `ip addr` / `ifconfig` (Linux/Mac).
   Algo como `192.168.x.x`. **Nunca use `localhost`**: para o ESP32, localhost é ele mesmo.
2. Copie `seguranca-viaria-recife/include/segredos.exemplo.h` para `segredos.h` e preencha
   Wi-Fi e `SERVIDOR_IP`.
3. Libere as portas 1883 e 8000 no firewall do PC.
4. ESP32 e PC precisam estar na mesma rede. Rede da faculdade costuma bloquear: use o hotspot do celular.

## 3. Ver se está funcionando

```bash
pip install paho-mqtt
python simular_painel.py --host <ip-do-pc>              # mostra a telemetria a cada 1 s
python simular_painel.py --host <ip-do-pc> silenciar    # testa comando painel -> módulo
```

Ou, com Mosquitto instalado: `mosquitto_sub -h <ip-do-pc> -t "cttu/#" -v`

Para trocar os trechos ao vivo: edite `api-mock/trechos.json` e rode
`python simular_painel.py --host <ip-do-pc> atualizar_trechos`.

## Problemas comuns

| Sintoma no serial | Causa provável |
|---|---|
| `[WiFi] reconectando...` sem parar | SSID/senha errados, ou rede 5 GHz (o ESP32 só pega 2,4 GHz) |
| `[MQTT] falhou (rc=-2)` | IP errado, firewall, ou Mosquitto sem `listener 1883 0.0.0.0` |
| `[API] falhou (-1)` | API fora do ar ou porta 8000 bloqueada; o módulo segue com os trechos de reserva |
| `"ts":0` na telemetria | Sem NTP (sem internet); o resto funciona |
