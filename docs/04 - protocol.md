# UDP protocol v1

One UTF-8 JSON object per datagram, maximum 1,200 bytes including whitespace.
The shared C++20 API is in `common/include/common/protocol.h`, provided by the
`common::protocol` CMake target. `encode(Message)` returns a datagram;
`decode(string_view)` returns a variant of `Reading`, `Ack`, or `Reject`.
Both reject invalid inputs with `ProtocolError`, which exposes an `ErrorCode`.
Public types do not depend on a JSON library or server infrastructure.

## Server Transport

The `alert_server_core` CMake target provides `AlertServer` from
`server/daemon/include/alert_server.h`. Its constructor takes a `ReadingStore&`
that must outlive the server, and an endpoint (default: `0.0.0.0:8080`). It owns
the underlying `IUdpSocket` (`PosixUdpSocket` or `WindowsUdpSocket`, created by
`make_udp_socket()`); another implementation can be passed to the constructor instead.
`send(message, destination)` validates and encodes a message before sending it.
Blocking `receive()` returns an `AlertServer::ReceivedMessage` containing a typed `message`
and the original `sender` endpoint, which can be passed directly to `send`.
Protocol and socket exceptions propagate to the caller; a malformed datagram
is consumed without closing the socket. `receive()` validates incoming readings,
commits them to the store, then sends a QoS 1 ACK before returning.
Identical retries are ACKed again without inserting another record. Conflicting
identities receive a reject without replacing the original, then raise a
`ProtocolError` rather than returning an accepted message. QoS 0 readings are
stored without ACKs. Database failures throw `PersistenceError` before any reply;
an ACK send failure leaves the committed reading available for a later retry.
Incoming ACKs and rejects are returned without storage or automatic replies.
The executable receives messages continuously and logs
malformed datagrams or database failures without stopping the receive loop.

## Persistence

The `persistency` CMake target provides `ReadingStore` from
`server/persistency/include/reading_store.h`. It uses the vendored SQLite library,
requests WAL journaling and FULL synchronous commits, and uses bound SQL
parameters. The executable creates `readings.sqlite3` in its working directory;
retain this database and its WAL files across restarts. A constructor path of
`:memory:` is supported for tests only and is not durable.

- `save(reading)` validates and stores the canonical protocol datagram, returning
  `Inserted`, `Duplicate`, or `IdentityConflict` using `(nodeId, sessionId, seq)`.
- `find(identity)` returns an optional reading, empty when the identity is unknown.

Readings are retained for restart recovery and deduplication. Processing queues,
alert evaluation and an MQTT outbox are not implemented. Existing databases with
the previous `processed` column remain compatible; that column is ignored.
Use each store instance from one application thread; independent connections
share the same database. Parent directories must already exist. Durability
depends on the filesystem and storage honoring SQLite's synchronization calls.

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