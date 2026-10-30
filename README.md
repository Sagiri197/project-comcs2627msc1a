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
