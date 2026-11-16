# Protocolo UDP

## Formato leitura

```json
{
  "version": 1,
  "kind": "reading",
  "qos": 1,
  "nodeId": "esp32-a",
  "sessionId": "c79e4fd8-5888-4ae5-8556-c1e558aa6020",
  "seq": 42,
  "timeSynchronized": true,
  "payload": {
    "id": "urn:ngsi-ld:WeatherObserved:esp32-a:c79e4fd8-5888-4ae5-8556-c1e558aa6020:42",
    "type": "WeatherObserved",
    "dateObserved": "2026-11-02T14:30:01Z",
    "location": {"type": "Point", "coordinates": [-8.608, 41.178]},
    "refDevice": "urn:ngsi-ld:Device:esp32-a",
    "temperature": 23.4,
    "relativeHumidity": 0.56
  }
}
```

A localização é meramente ilustrativa neste caso, mas achamos uma inclusão interessante para possiveis visualizações

- `nodeId`, `sessionId`: identificadores de ambos o nó que envia os dados e da sua sessão, sendo o `sessionId` um identificador de sessão gerado aleatoriamente no arranque
- `seq`: Número de sequência dentro da sessão, começando do zero
- `id` e `refDevice`: URNs com os prefixos mostrados acima
- `dateObserved`: momento da observação, UTC `YYYY-MM-DDTHH:MM:SSZ`
- `location`: Ponto GeoJSON
- `temperature`: valor em graus centigrados
- `relativeHumidity`: fração finita entre 0 e 1
- `timeSynchronized`: booleano opcional. Se falso, o timestamp não é confiável e não deve ser usado para comparações de tempo entre nós

## Respostas

```json
{"version":1,"kind":"ack","nodeId":"esp32-a","sessionId":"boot-1","seq":42}
```

```json
{"version":1,"kind":"reject","nodeId":"esp32-a","sessionId":"boot-1","seq":42,"code":"invalid_payload","reason":"Invalid observation"}
```

Códigos de rejeição: `invalid_message`, `unsupported_version`, `invalid_payload`, `identity_conflict`

## QoS

QoS 0 significa enviar uma vez, sem ACK. QoS 1 significa entrega pelo menos uma vez, tentando novamente até ser reconhecido.