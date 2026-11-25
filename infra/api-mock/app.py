"""
API falsa (mock) para testar o módulo sem depender da trilha de ML.

Entrega exatamente o formato da seção 1 de contratos.md. Quando a API real
existir, basta apontar API_URL_TRECHOS (firmware) para ela: nada mais muda.

Edite trechos.json e chame GET /modulo/trechos de novo (ou mande o comando
"atualizar_trechos" pelo MQTT) para o módulo receber a lista nova.
"""
import json
from pathlib import Path

from fastapi import FastAPI

ARQUIVO = Path(__file__).with_name("trechos.json")
app = FastAPI(title="API mock - Projeto 6 G3")


@app.get("/modulo/trechos")
def trechos():
    return json.loads(ARQUIVO.read_text(encoding="utf-8"))


@app.get("/")
def saude():
    return {"ok": True, "trechos": "/modulo/trechos"}
