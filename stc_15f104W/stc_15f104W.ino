#include <PubSubClient.h>
#include <ESP8266WiFi.h>

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

char mqttServer[]  = "192.168.1.20"; //change it for your MQTT server IP or network name
int mqttPort  = 1883; //1883 is the default port for MQTT. Change if necessary
char deviceId[]  = "myRelay-001"; //every device should have a different name
char topic[]  = "/myRelay-001/command"; //the topic should be different for each device as well
char mqttUser[]  = "yourMQTTuser";
char mqttPassword[]  = "yourMQTTpassword";

WiFiClient wifiClient;

PubSubClient client(wifiClient);

int status = WL_IDLE_STATUS;

void setup() {
  Serial.begin(9600);
  delay(10);
  Serial.println("Let' start now");
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
byte close[] = {0xA0, 0x01, 0x01, 0xA2};
byte open[] = {0xA0, 0x01, 0x00, 0xA1};

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
        lastOperation = now;
      } else if(message == closeString) {
         Serial.println("Performing close command");
        Serial.write(close, sizeof(close)); 
        lastOperation = now;
      }
  } else {
    Serial.println("Operation denied right now. Try again later.");
  }
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
    } else {
      Serial.print("[FAILED] [ rc = ");
      Serial.print(client.state() );
      Serial.println(" : retrying in 5 seconds]");
      delay(5000);
    }
  }
}
