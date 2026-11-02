# UDP protocol v1

One UTF-8 JSON object per datagram, maximum 1,200 bytes including whitespace.
The shared C++17 API is in `common/include/common/protocol.h`, provided by the
`common::protocol` CMake target. `encode(Message)` returns a datagram;
`decode(string_view)` returns a variant of `Reading`, `Ack`, or `Reject`.
Both reject invalid inputs with `ProtocolError`, which exposes an `ErrorCode`.
Public types do not depend on a JSON library or server infrastructure.

## Reading

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

The coordinates are illustrative. This is a WeatherObserved key-values profile,
not an implementation of every property in the Smart Data Models schema.

- `version`, `qos`, and `seq` must be JSON integers, not strings or decimals.
- `nodeId` and `sessionId`: 1-64 ASCII letters, digits, dots, underscores or hyphens.
  Generate a random session identifier on boot; UUID strings are recommended.
- `seq`: unsigned 32-bit integer, including zero. Start a new session before
  sequence exhaustion. Identity is the tuple `(nodeId, sessionId, seq)`.
- `id` and `refDevice`: nonempty typed URNs with the prefixes shown above,
  maximum 256 bytes each, with no whitespace or non-ASCII characters.
- `dateObserved`: actual observation time, UTC `YYYY-MM-DDTHH:MM:SSZ`, optionally
  with 1-9 fractional digits before `Z`. Calendar dates are checked; leap seconds
  and timezone offsets are not supported.
- `location`: GeoJSON Point, exactly `[longitude, latitude]`, bounded by
  `[-180, 180]` and `[-90, 90]` respectively.
- `temperature`: finite Celsius value. `relativeHumidity`: finite fraction in
  `[0, 1]`, so 56% is `0.56`.
- `timeSynchronized`: optional boolean, default `true`. If false, the timestamp
  is untrusted and must not be used for cross-node time comparisons.

Equipment violations (temperature outside `[0, 50]`, humidity outside
`[0.20, 0.80]`) are accepted for later alert evaluation, not rejected by the codec.
Unknown fields are ignored and are not preserved by re-encoding. Duplicate
object keys, raw NUL bytes, invalid UTF-8 and trailing non-whitespace are rejected.

## Responses

```json
{"version":1,"kind":"ack","nodeId":"esp32-a","sessionId":"boot-1","seq":42}
```

```json
{"version":1,"kind":"reject","nodeId":"esp32-a","sessionId":"boot-1","seq":42,"code":"invalid_payload","reason":"Invalid observation"}
```

Reject codes: `invalid_message`, `unsupported_version`, `invalid_payload`,
`identity_conflict`. Reasons must be 1-256 UTF-8 bytes without control characters.
Only send correlated responses when the incoming identity can be trusted and
validated; malformed JSON may not contain enough information to respond.

## Delivery Contract

QoS 0 means send once, without an ACK. QoS 1 means at-least-once delivery:
persist before sending, retry until acknowledged, and keep the original
identity, payload and timestamp through retries and reboot recovery.
The server must commit durable acceptance before acknowledging QoS 1 and
deduplicate on identity. An identical duplicate is acknowledged again; a
different observation with the same identity is an identity conflict.
MQTT delivery is tracked independently; a UDP ACK does not confirm MQTT delivery.

The codec does not implement retries, persistence, deduplication, authentication,
clock synchronization, node registration, alert evaluation or response policy.
Those responsibilities belong to the respective node/server application layers.