// Minimal Node-RED settings for the COMCS command centre.
// Runs only inside docker compose. The MQTT broker host/port come from the environment
// (docker-compose.yml); the defaults are the compose service name and port.
process.env.MQTT_HOST = process.env.MQTT_HOST || 'mosquitto';
process.env.MQTT_PORT = process.env.MQTT_PORT || '1883';

module.exports = {
    flowFile: 'flows.json',
    flowFilePretty: true,
    // The broker is anonymous, so there are no secrets to encrypt in flows_cred.json.
    credentialSecret: false,
    uiPort: process.env.PORT || 1880,
    diagnostics: { enabled: false },
    runtimeState: { enabled: false, ui: false },
    logging: {
        console: { level: 'info', metrics: false, audit: false }
    },
    editorTheme: {
        projects: { enabled: false }
    },
    functionExternalModules: false,
    functionTimeout: 0,
    debugMaxLength: 1000
};
