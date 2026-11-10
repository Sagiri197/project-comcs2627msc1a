# Trabalho comics

```sh
git submodule update --init --recursive
```

## Repositório

- `/.scripts`: Scripts auxiliares
- `/docs`: Documentação do projecto
- `/common`: Build CMake e testes do protocolo partilhado em `/nodes/sketch/src/common`
- `/nodes`: Sketches para correr nos ESP32s
- `/server`: Servidor UDP de alerta (só o programa: código C++, build e testes)
- `/access-point`: Rede Wi-Fi da célula (access point, DHCP/DNS, NTP): `linux/` para o Raspberry Pi, `windows/` para testar num portátil
- `/command-centre`: Broker MQTT e Node-RED

## Dependências

### Servidor UDP

- [CMake](https://cmake.org/)
- [spdlog](https://github.com/gabime/spdlog)
- [Eclipse Mosquitto/libmosquitto](https://github.com/eclipse-mosquitto/mosquitto)
- [SQLite](https://github.com/azadkuh/sqlite-amalgamation)
- [GoogleTest](https://github.com/google/googletest) (testes)
- [nlohmann/json](https://github.com/nlohmann/json)
- [Doxygen](https://www.doxygen.nl/) (opcional)

### Firmware ESP32

- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library)
- [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor)
- [Gotek LEDC68](https://github.com/coopzone-dc/GotekLEDC68)

### Command Centre
- [Docker](https://www.docker.com/) (para correr o broker MQTT e Node-RED)

## Arrancar tudo

1. Command Centre (broker MQTT + Node-RED):
   ```sh
   cd command-centre && docker compose up -d --build
   ```
2. Access point (noutro terminal; Ctrl+C para parar):
   ```sh
   access-point\windows\access-point.cmd     # Linux: access-point/linux/access-point.sh
   ```
3. Servidor de alerta (noutro terminal, sempre a partir da mesma pasta):
   ```sh
   server\build\bin\Release\alert_server.exe --config server/config/alert_server.ini  # Linux: server/build/bin/alert_server
   ```
4. a. Flash e monitor de cada ESP32 (uma por terminal):
   ```sh
   make -C nodes flash-monitor NODE=1         # NODE=2, NODE=3
   ```
   b. Flashar todos os ESP32s (Requere makefile configurado com portos) de uma vez:
   ```sh
   make -C nodes flash
   ```

## Build e testagem

```sh
cmake -S server -B server/build -DBUILD_TESTING=ON
cmake --build server/build -j2
ctest --test-dir server/build --output-on-failure
./server/build/bin/alert_server --config server/config/alert_server.ini
```

### Documentação

[https://sagiri197.github.io/project-comcs2627msc1a/](https://sagiri197.github.io/project-comcs2627msc1a/)

```sh
cmake -S server -B server/build
cmake --build server/build --target doxygen
```

## Buildar sketch ESP32

Abrir `nodes/sketch/sketch.ino` diretamente no Arduino IDE. Todos os fontes
locais ficam dentro do sketch; nao e necessario executar scripts de preparacao.
Instalar o pacote de placas ESP32 da Espressif (3.x ou superior) e as bibliotecas
DHT sensor library e Adafruit Unified Sensor. Selecionar a placa e a porta.

### Configuracao de cada ESP32

Os valores especificos de cada dispositivo ficam em ficheiros separados:

- `nodes/sketch/src/configs/esp32_G.h`: ID `esp32-001`, com a rede e posicao existentes.
- `nodes/sketch/src/configs/esp32_P.h`: ID `esp32-002`.
- `nodes/sketch/src/configs/esp32_T.h`: ID `esp32-003`.

Antes de cada upload, escolher o dispositivo em `nodes/sketch/src/config.h`, alterando
o valor por defeito de `NODE_PROFILE` para `1`, `2` ou `3`:

### ESP32 esquemáticas![](./docs/images/esp32_controller.svg)