#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

const char mqttServer[]  = "192.168.1.20"; //change it for your MQTT server IP or network name
const int mqttPort  = 1883; //1883 is the default port for MQTT. Change if necessary
const char deviceId[]  = "myRelay-001"; //every device should have a different name
const char commandTopic[]  = "/myRelay-001/command"; //the topic should be different for each device as well
const char mqttUser[]  = "yourMQTTuser";
const char mqttPassword[]  = "yourMQTTpassword";
const uint8_t mqttQos = 0;

char hass_state_topic[strlen(deviceId) + 5 + 1] = { 0 };       // Topic in will be: home/<deviceId>
char hass_command_topic[strlen(deviceId) + 5 + 8 + 1] = { 0 }; // Topic out will be: home/<deviceId>/command
char hass_topic[strlen(deviceId) + 28 + 1] = { 0 };            // Topic hass will be: homeassistant/switch/<deviceId>/config
char hass_payload[strlen(deviceId) * 3 + 107 + 1] = { 0 };     // Payload will be {\"name\":\"<deviceId>\",\"command_topic\":\"home/<deviceId>/command\",\"state_topic\":\"home/<deviceId>\",\"payload_off\":\"CLOSE\",\"payload_on\":\"OPEN\"}

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void setup() {
  Serial.begin(9600);
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

  snprintf(hass_state_topic,   sizeof(hass_state_topic),   "home/%s", deviceId);
  snprintf(hass_command_topic, sizeof(hass_command_topic), "home/%s/command", deviceId);
  snprintf(hass_topic,         sizeof(hass_topic),         "homeassistant/switch/%s/config", deviceId);
  snprintf(hass_payload,       sizeof(hass_payload),       "{\"name\":\"%s\",\"command_topic\":\"home/%s/command\",\"state_topic\":\"home/%s\",\"payload_off\":\"CLOSE\",\"payload_on\":\"OPEN\"}", deviceId, deviceId, deviceId);

  connectToWifi();
}

void loop() {
}

unsigned long lastOperation = 0;
const long MIN_OPERATION_INTERVAL = 2000L;

const String openString = "OPEN";
const String closeString = "CLOSE";
const byte close[] = {0xA0, 0x01, 0x01, 0xA2};
const byte open[] = {0xA0, 0x01, 0x00, 0xA1};

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
  doHADiscovery();
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
  if (strcmp(topic, hass_command_topic) == 0 || strcmp(topic, commandTopic) == 0) {
    Serial.println("Incoming message:");
    Serial.print("  topic: ");
    Serial.println(topic);
    Serial.print("  payload len: ");
    Serial.println(len);
    processMsg(payload, len);
  }
}

void processMsg(char* payload, size_t len) {
  if (len == openString.length() || len == closeString.length()) {
    signed long now = millis();
    long deltaTime = now - lastOperation;
    if (deltaTime < MIN_OPERATION_INTERVAL) {
      Serial.println("Operation denied right now. Try again later.");
      return;
    }

    String message = "";
    for (int i = 0; i < len; i++) {
      message = message + (char)payload[i];
    }
    Serial.print("  payload: ");
    Serial.println(message);

    if(message == openString) {
      Serial.println("  -> Performing open command");
      Serial.write(open, sizeof(open));
      mqttClient.publish(hass_state_topic, mqttQos, true, openString.c_str());
    } else if(message == closeString) {
      Serial.println("  -> Performing close command");
      Serial.write(close, sizeof(close));
      mqttClient.publish(hass_state_topic, mqttQos, true, closeString.c_str());
    }
    lastOperation = now;
  }
}

void doHADiscovery() {
  // subscribe to command topic
  if (mqttClient.subscribe(hass_command_topic, mqttQos)) {
    Serial.print("HASS: successfully subscribed to ");
  } else {
    Serial.print("HASS: failed to subscribed to ");
  }
  Serial.println(hass_command_topic);

  if (mqttClient.publish(hass_topic, mqttQos, true, hass_payload)) {
    Serial.print("HASS: successfully published to ");
  } else {
    Serial.print("HASS: failed to published to ");
  }
  Serial.println(hass_topic);

  // finally send default state
  if (mqttClient.publish(hass_state_topic, mqttQos, true, closeString.c_str())) {
    Serial.print("HASS: successfully published to ");
  } else {
    Serial.print("HASS: failed to published to ");
  }
  Serial.println(hass_command_topic);
}
