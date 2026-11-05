# Trabalho comics

```sh
git submodule update --init --recursive
```

## Repositório

- `/docs`: Documentação do projecto
- `/nodes`: Sketches para correr nos ESP32s
- `/server`: Servidor UDP de alerta

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