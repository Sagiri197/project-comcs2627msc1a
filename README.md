# Trabalho comics

```sh
git submodule update --init --recursive
```

## Repositório

- `/docs`: Documentação do projecto
- `/common`: Protocolo partilhado entre nodes e servidor
- `/nodes`: Sketches para correr nos ESP32s
- `/server`: Servidor UDP de alerta (só o programa: código C++, build e testes)
- `/access-point`: Rede Wi-Fi da célula (access point, DHCP/DNS, NTP): `linux/` para o Raspberry Pi, `windows/` para testar num portátil
- `/command-centre`: Broker MQTT e Node-RED

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
   server\build\bin\Release\alert_server.exe  # Linux: server/build/bin/alert_server
   ```
4. Flash e monitor de cada ESP32 (uma por terminal):
   ```sh
   make -C nodes flash-monitor NODE=1         # NODE=2, NODE=3
   ```

## Build e testagem

```sh
cmake -S server -B server/build -DBUILD_TESTING=ON
cmake --build server/build -j2
ctest --test-dir server/build --output-on-failure
./server/build/bin/alert_server
```

## Buildar sketch ESP32

Criar uma pasta `sketch`, copiar os conteudos de `nodes` para lá, e
copiar commons `common` para `sketch/src/common`:

### Configuracao de cada ESP32

Os valores especificos de cada dispositivo ficam em ficheiros separados:

- `nodes/configs/esp32_001.h`: ID `esp32-001`, com a rede e posicao existentes.
- `nodes/configs/esp32_002.h`: ID `esp32-002`.
- `nodes/configs/esp32_003.h`: ID `esp32-003`.

Antes de cada upload, escolher o dispositivo em `sketch/config.h`, alterando
o valor por defeito de `NODE_PROFILE` para `1`, `2` ou `3`: