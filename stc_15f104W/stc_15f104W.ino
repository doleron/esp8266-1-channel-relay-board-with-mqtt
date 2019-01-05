#include <PubSubClient.h>
#include <ESP8266WiFi.h>

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

const char mqttServer[]  = "192.168.1.20"; //change it for your MQTT server IP or network name
const int mqttPort  = 1883; //1883 is the default port for MQTT. Change if necessary
const char deviceId[]  = "myRelay-001"; //every device should have a different name
const char topic[]  = "/myRelay-001/command"; //the topic should be different for each device as well
const char mqttUser[]  = "yourMQTTuser";
const char mqttPassword[]  = "yourMQTTpassword";

char hass_state_topic[strlen(deviceId) + 5 + 1] = { 0 };       // Topic in will be: home/<deviceId>
char hass_command_topic[strlen(deviceId) + 5 + 8 + 1] = { 0 }; // Topic out will be: home/<deviceId>/command
char hass_topic[strlen(deviceId) + 28 + 1] = { 0 };            // Topic hass will be: homeassistant/switch/<deviceId>/config
char hass_payload[strlen(deviceId) * 3 + 107 + 1] = { 0 };     // Payload will be {\"name\":\"<deviceId>\",\"command_topic\":\"home/<deviceId>/command\",\"state_topic\":\"home/<deviceId>\",\"payload_off\":\"CLOSE\",\"payload_on\":\"OPEN\"}

WiFiClient wifiClient;

PubSubClient client(wifiClient);

int status = WL_IDLE_STATUS;

void setup() {
  Serial.begin(9600);
  delay(10);
  Serial.println("Let's start now.");

  snprintf(hass_state_topic,   sizeof(hass_state_topic),   "home/%s", deviceId);
  snprintf(hass_command_topic, sizeof(hass_command_topic), "home/%s/command", deviceId);
  snprintf(hass_topic,         sizeof(hass_topic),         "homeassistant/switch/%s/config", deviceId);
  snprintf(hass_payload,       sizeof(hass_payload),       "{\"name\":\"%s\",\"command_topic\":\"home/%s/command\",\"state_topic\":\"home/%s\",\"payload_off\":\"CLOSE\",\"payload_on\":\"OPEN\"}", deviceId, deviceId, deviceId);
  Serial.println("Home Assistant variables set.");
}

void loop() {
  if ( !client.connected() ) {
    connect();
  }
  client.loop();
}

unsigned long lastOperation;
const long MIN_OPERATION_INTERVAL = 2000L;

const String openString = "OPEN";
const String closeString = "CLOSE";
const byte close[] = {0xA0, 0x01, 0x01, 0xA2};
const byte open[] = {0xA0, 0x01, 0x00, 0xA1};

void callback(char* topic, byte* payload, unsigned int length) {
    signed long now = millis();
    long deltaTime = now - lastOperation;
    if (deltaTime > MIN_OPERATION_INTERVAL) {
      String message = "";
      for (int i = 0; i < length; i++) {
        message = message + (char)payload[i];
      }
      if(message == openString) { 
        Serial.println("Performing open command");
        Serial.write(open, sizeof(open));
        client.publish(hass_state_topic, "OPEN", true);
        lastOperation = now;
      } else if(message == closeString) {
        Serial.println("Performing close command");
        Serial.write(close, sizeof(close)); 
        client.publish(hass_state_topic, "CLOSE", true);
        lastOperation = now;
      }
  } else {
    Serial.println("Operation denied right now. Try again later.");
  }
}

void doHADiscovery() {
  // subscribe to command topic
  if (client.subscribe(hass_command_topic)) {
    Serial.print("HASS: successfully subscribed to ");
  } else {
    Serial.print("HASS: failed to subscribed to ");
  }
  Serial.println(hass_command_topic);

#if 1
  // client.publish(hass_topic, hass_payload, true); is too long!
  // McLighting could fix this somehow..
  const uint16_t lenght = sizeof(hass_payload) - 1;

  client.beginPublish(hass_topic, lenght, true);
  for (uint16_t i = 0; i < sizeof(hass_payload); i += 10) {
    uint16_t toSend = i + 10 > lenght ? lenght - i : 10;
    client.write((uint8_t *)&hass_payload[i], toSend);
  }
  client.endPublish();
  Serial.print("HASS: successfully published to ");
  Serial.println(hass_topic);
#else
  if (client.publish(hass_topic, hass_payload, true)) {
    Serial.print("HASS: successfully published to ");
  } else {
    Serial.print("HASS: failed to published to ");
  }
  Serial.println(hass_topic);
#endif

  // finally send default state
  if (client.publish(hass_state_topic, "CLOSE", true)) {
    Serial.print("HASS: successfully published to ");
  } else {
    Serial.print("HASS: failed to published to ");
  }
  Serial.println(hass_command_topic);
}

void connect() {
  while (!client.connected()) {
    status = WiFi.status();
    if ( status != WL_CONNECTED) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.print("Connected to ");
      Serial.println(WIFI_SSID);
    }

    client.setServer(mqttServer, mqttPort);
    client.setCallback(callback);
    if (client.connect(deviceId, mqttUser, mqttPassword)) {
      client.subscribe(topic);
      Serial.println("Connected to MQTT Server");
      doHADiscovery();
    } else {
      Serial.print("[FAILED] [ rc = ");
      Serial.print(client.state() );
      Serial.println(" : retrying in 5 seconds]");
      delay(5000);
    }
  }
}
