// Copie este arquivo para include/segredos.h e preencha.
// segredos.h está no .gitignore e não vai para o GitHub.
#pragma once

#define WIFI_SSID   "nome-da-rede"
#define WIFI_SENHA  "senha-da-rede"

// IP do PC que roda o docker-compose (veja infra/README.md para descobrir)
#define SERVIDOR_IP "192.168.0.10"

// Descomente para fixar a hora local na demo (ex.: 18h) se o NTP não funcionar
// #define HORA_DEMO 18
