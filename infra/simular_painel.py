"""
Faz o papel do painel no terminal: mostra a telemetria do módulo e manda comandos.

  pip install paho-mqtt
  python simular_painel.py                 # só escuta
  python simular_painel.py silenciar       # manda um comando e continua escutando
  python simular_painel.py --host 192.168.0.10 ativar_som

Comandos: silenciar | ativar_som | atualizar_trechos
"""
import argparse
import json

import paho.mqtt.client as mqtt

p = argparse.ArgumentParser()
p.add_argument("comando", nargs="?", choices=["silenciar", "ativar_som", "atualizar_trechos"])
p.add_argument("--host", default="localhost")
p.add_argument("--modulo", default="01")
a = p.parse_args()

base = f"cttu/modulo/{a.modulo}"


def ao_conectar(c, *_):
    c.subscribe(f"{base}/#")
    print(f"conectado a {a.host}, ouvindo {base}/#")
    if a.comando:
        c.publish(f"{base}/comando", json.dumps({"acao": a.comando}))
        print(f"-> comando enviado: {a.comando}")


def ao_receber(c, _, msg):
    print(f"{msg.topic}: {msg.payload.decode(errors='replace')}")


c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect = ao_conectar
c.on_message = ao_receber
c.connect(a.host, 1883)
c.loop_forever()
