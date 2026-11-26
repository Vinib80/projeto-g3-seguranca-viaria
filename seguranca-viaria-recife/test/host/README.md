# Testes da lógica no PC

Testam `include/logica.h` (zonas de risco, alerta, JSON do contrato) sem precisar do ESP32.

```bash
# baixe o ArduinoJson.h (arquivo único) uma vez:
curl -L -o /tmp/ArduinoJson.h https://github.com/bblanchon/ArduinoJson/releases/download/v7.2.1/ArduinoJson-v7.2.1.h
g++ -std=c++17 -I include -I /tmp test/host/teste_logica.cpp -o teste && ./teste
```
(rodar de dentro de `seguranca-viaria-recife/`)
