"""
Faz o papel do módulo (ESP32) no terminal, para testar o dashboard sem a placa.

Publica a telemetria de contratos.md (seção 2) a cada 1 s, o status retido (seção 4) e
obedece aos comandos do painel (seção 3). Percorre uma rota norte-sul que atravessa os
trechos de api-mock/trechos.json, com a mesma regra do firmware: fora = verde,
dentro até vel_segura = amarelo, acima = vermelho.

  pip install paho-mqtt
  python simular_modulo.py                     # broker local (docker compose up)
  python simular_modulo.py --host broker.hivemq.com
  python simular_modulo.py --teste             # só a auto-checagem, sem rede
"""
import argparse
import json
import math
import time
from pathlib import Path

TRECHOS = json.loads((Path(__file__).parent / "api-mock" / "trechos.json").read_text(encoding="utf-8"))
# ponytail: rota fixa ao longo da lon dos trechos de exemplo; trocar se o trechos.json mudar de lugar.
ROTA = [(-8.0700 + i * 0.0006, -34.8829) for i in range(41)]
ROTA += ROTA[-2:0:-1]  # ida e volta


def distancia_m(lat1, lon1, lat2, lon2):
    r, rad = 6371000, math.pi / 180
    a = (math.sin((lat2 - lat1) * rad / 2) ** 2
         + math.cos(lat1 * rad) * math.cos(lat2 * rad) * math.sin((lon2 - lon1) * rad / 2) ** 2)
    return 2 * r * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def trecho_em(lat, lon, hora):
    for t in TRECHOS:
        if (not t["horas"] or hora in t["horas"]) and distancia_m(lat, lon, t["lat"], t["lon"]) <= t["raio_m"]:
            return t
    return None


def alerta(trecho, vel):
    if not trecho:
        return "verde"
    return "vermelho" if vel > trecho["vel_segura"] else "amarelo"


def teste():
    sul = TRECHOS[0]
    assert trecho_em(sul["lat"], sul["lon"], 3)["id"] == sul["id"]
    assert trecho_em(-8.0700, -34.8829, 3) is None
    assert alerta(None, 90) == "verde"
    assert alerta(sul, sul["vel_segura"]) == "amarelo"
    assert alerta(sul, sul["vel_segura"] + 1) == "vermelho"
    print("ok")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="localhost")
    p.add_argument("--modulo", default="01")
    p.add_argument("--teste", action="store_true")
    a = p.parse_args()
    if a.teste:
        return teste()

    import paho.mqtt.client as mqtt

    base = f"cttu/modulo/{a.modulo}"
    som = {"ligado": True}

    def ao_conectar(c, *_):
        c.publish(f"{base}/status", "online", qos=1, retain=True)
        c.subscribe(f"{base}/comando")

    def ao_comando(c, _, msg):
        acao = json.loads(msg.payload or b"{}").get("acao")
        if acao in ("silenciar", "ativar_som"):
            som["ligado"] = acao == "ativar_som"
        print(f"[CMD] {acao}")

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"sim-modulo-{a.modulo}")
    c.will_set(f"{base}/status", "offline", qos=1, retain=True)
    c.on_connect = ao_conectar
    c.on_message = ao_comando
    c.connect(a.host, 1883)
    c.loop_start()
    print(f"publicando em {a.host}: {base}/telemetria (Ctrl+C para parar)")
    try:
        i = 0
        while True:
            lat, lon = ROTA[i % len(ROTA)]
            vel = int(40 + 15 * math.sin(i / 6))  # oscila entre 25 e 55 km/h
            t = trecho_em(lat, lon, time.localtime().tm_hour)
            msg = {"ts": int(time.time()), "lat": round(lat, 5), "lon": lon, "vel_kmh": vel,
                   "trecho_id": t["id"] if t else None, "alerta": alerta(t, vel)}
            c.publish(f"{base}/telemetria", json.dumps(msg), qos=0)
            bipe = " (bipe)" if msg["alerta"] == "vermelho" and som["ligado"] else ""
            print(f"{vel:3d} km/h  {msg['trecho_id'] or '-':13}  {msg['alerta']}{bipe}")
            i += 1
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        c.publish(f"{base}/status", "offline", qos=1, retain=True).wait_for_publish(3)
        c.loop_stop()


if __name__ == "__main__":
    main()
