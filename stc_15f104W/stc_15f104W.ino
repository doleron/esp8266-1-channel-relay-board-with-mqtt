#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

/*
 * configurations
 */

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// This enables MQTT auto discovery for Home Assistant
#define ENABLE_HOMEASSISTANT 1
// This enables the Home Assistant device registry (experimental)
#define ENABLE_HA_DEVICE_REGISTRY 0

const char deviceId[]     = "myRelay-001"; //every device should have a different name
const char commandTopic[] = "/myRelay-001/command"; //the topic should be different for each device as well

const char    mqttServer[]   = "192.168.1.20"; //change it for your MQTT server IP or network name
const int     mqttPort       = 1883; //1883 is the default port for MQTT. Change if necessary
const char    mqttUser[]     = "yourMQTTuser";
const char    mqttPassword[] = "yourMQTTpassword";
const uint8_t mqttQos        = 0; // At most once (0), At least once (1), Exactly once (2).

const uint8_t numRelays = 2;

/*
 * My single relay board requires 9600
 * My double relay baord requires 115200
 */
const uint32_t serialSpeed = 115200;

/*
 * end of configuration
 */

unsigned long lastOperation = 0;
const long MIN_OPERATION_INTERVAL = 1000L;

const char openString[] = "OPEN";
const char closeString[] = "CLOSE";

/*
 * Spec:
 * Open  the 1st relay： A0 01 01 A2
 * Close the 1st relay： A0 01 00 A1
 * Open  the 2nd relay： A0 02 01 A3
 * Close the 2nd relay： A0 02 00 A2
 * Open  the 3rd relay： A0 03 01 A4
 * Close the 3rd relay： A0 03 00 A3
 * Open  the 4th relay： A0 04 01 A5
 * Close the 4th relay： A0 04 00 A4
*/
const byte closeCommand[][4] = {
  {0xA0, 0x01, 0x01, 0xA2},
  {0xA0, 0x02, 0x01, 0xA3},
  {0xA0, 0x03, 0x01, 0xA4},
  {0xA0, 0x04, 0x01, 0xA5}
};
const byte openCommand[][4]  = {
  {0xA0, 0x01, 0x00, 0xA1},
  {0xA0, 0x02, 0x00, 0xA2},
  {0xA0, 0x03, 0x00, 0xA3},
  {0xA0, 0x04, 0x00, 0xA4}
};

// sanity checks
static_assert(numRelays < 10, "numRelays cannot be greater than 9!");
static_assert(sizeof(closeCommand) / sizeof(closeCommand[0]) >= numRelays, "numRelays is greater than number of supported relay commands (close)!");
static_assert(sizeof(openCommand)  / sizeof(openCommand[0])  >= numRelays, "numRelays is greater than number of supported relay commands (open)!");

#if ENABLE_HOMEASSISTANT
const char tmplt_hassStateTopic[] =       "home/%s/%d";
const char tmplt_hassCommandTopic[] =     "home/%s/%d/command";
const char tmplt_hassDiscoveryTopic[] =   "homeassistant/switch/%s/%d/config";
#if ENABLE_HA_DEVICE_REGISTRY
const char tmplt_hassDiscoveryPayload[] = "{\"name\":\"%s.%d\",\"uniq_id\":\"%s_%d\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"pl_off\":\"%s\",\"pl_on\":\"%s\",\"device\":{\"identifiers\":\"%s\",\"name\":\"%s\",\"sw_version\":\"0.1\",\"model\":\"ESP8266-01\",\"manufacturer\":\"XYZ\"}}";
#else // ENABLE_HA_DEVICE_REGISTRY
const char tmplt_hassDiscoveryPayload[] = "{\"name\":\"%s.%d\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"pl_off\":\"%s\",\"pl_on\":\"%s\"}";
#endif // ENABLE_HA_DEVICE_REGISTRY

#define L(tmplt,numInserts) (strlen(tmplt) + strlen(deviceId) - strlen("%s") * numInserts + 1) // + 1 for \0 at the end of the string
char hassStateTopic[L(tmplt_hassStateTopic, 2) + 1] = { 0 };         // + 1 for the inserted number
char hassCommandTopic[L(tmplt_hassCommandTopic, 2) + 1] = { 0 };     // + 1 for the inserted number
char hassDiscoveryTopic[L(tmplt_hassDiscoveryTopic, 2) + 1] = { 0 }; // + 1 for the inserted number
#if ENABLE_HA_DEVICE_REGISTRY
char hassDiscoveryPayload[L(tmplt_hassDiscoveryPayload, 10)
                          + strlen(deviceId) * 3 + 2     // "name" and "unique_id" ( <deviceId>.X / <deviceId>_X )
#else // ENABLE_HA_DEVICE_REGISTRY
char hassDiscoveryPayload[L(tmplt_hassDiscoveryPayload, 6)
                          + strlen(deviceId) + 1         // "name" (<deviceId>.X)
#endif // ENABLE_HA_DEVICE_REGISTRY
                          + sizeof(hassStateTopic) - 1   // - 1 due to \0
                          + sizeof(hassCommandTopic) - 1 // - 1 due to \0
                          + strlen(openString) + strlen(closeString)
                         ] = { 0 };
#undef L

const uint8_t relayNumPos = sizeof(hassStateTopic) - 2; // - 1 for trailing \0, - 1 last character
#endif // ENABLE_HOMEASSISTANT

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void setup() {
  Serial.begin(serialSpeed);
  delay(10);
  Serial.println("Let's start now.");

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(mqttServer, mqttPort);
  if (mqttUser != "" or mqttPassword != "")
    mqttClient.setCredentials(mqttUser, mqttPassword);

  connectToWifi();
}

void loop() {}

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  mqttClient.subscribe(commandTopic, mqttQos);
#if ENABLE_HOMEASSISTANT
  doHADiscovery();
#endif // ENABLE_HOMEASSISTANT
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("Disconnected from MQTT, reason: ");
  if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
    Serial.println("Bad server fingerprint.");
  } else if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED) {
    Serial.println("TCP Disconnected.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION) {
    Serial.println("Bad server fingerprint.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED) {
    Serial.println("MQTT Identifier rejected.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE) {
    Serial.println("MQTT server unavailable.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS) {
    Serial.println("MQTT malformed credentials.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED) {
    Serial.println("MQTT not authorized.");
  } else if (reason == AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE) {
    Serial.println("Not enough space on esp8266.");
  }

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Incoming message:");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  payload len: ");
  Serial.println(len);

  uint8_t relayNum = (uint8_t)(topic[relayNumPos] - '0');
  Serial.print("  relay: ");
  Serial.println(relayNum);

  if (relayNum > numRelays) {
    Serial.println("Invalid relay number!");
    return;
  }

  processMsg(payload, len, relayNum);
}

void processMsg(char* payload, size_t len, uint8_t relayNum) {
  if (len == strlen(openString) || len == strlen(closeString)) {
    signed long now = millis();
    long deltaTime = now - lastOperation;
    if (deltaTime < MIN_OPERATION_INTERVAL) {
      Serial.println("Operation denied right now. Try again later.");
      return;
    }

    payload[len] = '\0'; // ensure a proper end
    Serial.print("  payload: ");
    Serial.println(payload);

    if (strcmp(payload, openString) == 0) {
      Serial.println("  -> Performing open command");
      turnOn(true, relayNum);
    } else if (strcmp(payload, closeString) == 0) {
      Serial.println("  -> Performing close command");
      turnOn(false, relayNum);
    }

    lastOperation = now;
  }
}

void turnOn(bool active, uint8_t relayNum) {
  const char *state;
  const byte *cmd;

  // select values
  if (active)  {
    state = openString;
    cmd = openCommand[relayNum];
  } else  {
    state = closeString;
    cmd = closeCommand[relayNum];
  }

  // update state
  Serial.write(cmd, sizeof(cmd));

  // HA integration
#if ENABLE_HOMEASSISTANT
  hassStateTopic[relayNumPos] = relayNum + '0';
  mqttClient.publish(hassStateTopic, mqttQos, true, state);
#endif // ENABLE_HOMEASSISTANT
}

#if ENABLE_HOMEASSISTANT
void doHADiscovery() {
  for (int i = 0; i < numRelays; ++i) {
    // build topics
    snprintf(hassStateTopic,       sizeof(hassStateTopic),       tmplt_hassStateTopic,       deviceId, i);
    snprintf(hassCommandTopic,     sizeof(hassCommandTopic),     tmplt_hassCommandTopic,     deviceId, i);
    snprintf(hassDiscoveryTopic,   sizeof(hassDiscoveryTopic),   tmplt_hassDiscoveryTopic,   deviceId, i);
#if ENABLE_HA_DEVICE_REGISTRY
    snprintf(hassDiscoveryPayload, sizeof(hassDiscoveryPayload), tmplt_hassDiscoveryPayload, deviceId, i, deviceId, i, hassCommandTopic, hassStateTopic, openString, closeString, deviceId, deviceId);
#else // ENABLE_HA_DEVICE_REGISTRY
    snprintf(hassDiscoveryPayload, sizeof(hassDiscoveryPayload), tmplt_hassDiscoveryPayload, deviceId, i, hassCommandTopic, hassStateTopic, openString, closeString);
#endif ENABLE_HA_DEVICE_REGISTRY

    if (numRelays == 1) {
      // remove ".0" from name as there is only one relay
      // value is "{"name":"<deviceId>.<num>", ....
      hassDiscoveryPayload[strlen(deviceId) +  9] = '"'; // overwrite .
      hassDiscoveryPayload[strlen(deviceId) + 10] = ' '; // overwrite <num>
      hassDiscoveryPayload[strlen(deviceId) + 11] = ' '; // overwrite "
    }

    // subscribe to command topic
    if (mqttClient.subscribe(hassCommandTopic, mqttQos)) {
      Serial.print("HASS: successfully subscribed to ");
    } else {
      Serial.print("HASS: failed to subscribed to ");
    }
    Serial.println(hassCommandTopic);
    //Serial.println(hassDiscoveryPayload);

    if (mqttClient.publish(hassDiscoveryTopic, mqttQos, true, hassDiscoveryPayload)) {
      Serial.print("HASS: successfully published to ");
    } else {
      Serial.print("HASS: failed to published to ");
    }
    Serial.println(hassDiscoveryTopic);

    // finally send default state
    if (mqttClient.publish(hassStateTopic, mqttQos, true, openString)) {
      Serial.print("HASS: successfully published to ");
    } else {
      Serial.print("HASS: failed to published to ");
    }
    Serial.println(hassCommandTopic);
  }
}
#endif // ENABLE_HOMEASSISTANT
