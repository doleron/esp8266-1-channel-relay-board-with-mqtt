# Esp8266 1-Channel Relay Board with MQTT

Sketch to control an esp8266-01 STC 15f104W powered 1-channel relay board to be controlled remotely using a MQTT broker as communication bus.

## The board

I bought this chinese board in a local retailer and the device was delivered whitout any kind of technical instructions. It have a single channel 10 Amp Relay and a slot to plug an Esp8266 into. Besides voltage regulators and other passive components, the board have a CHIP STC 15f104W with is responsible for receive commands from the esp and directly command the relay.

![The board](https://raw.githubusercontent.com/doleron/esp8266-1-channel-relay-board-with-mqtt/master/images/relay_esp_board.JPG)

## The Esp8266-01

The Espressif Esp8266 01 is a revolutionary device that allows everyone build real IoT solutions at low cost. The one I have used in this example is the second generation of the model 01. It is compatible with the board slot layout and has 1 Mb of flash memory, a valuable resource to build customizable solutions and store persistent tracking records.

![Esp8266 01](https://raw.githubusercontent.com/doleron/esp8266-1-channel-relay-board-with-mqtt/master/images/esp8266-01.JPG)

## Trying to use the board

My first though when I was buying this device was: It should be easy to get up. Well, I was completely wrong. The leak of official documentation leave alone in an obscure journey of emptyness and doubt. After my searches I have found just two concrete (but ugly) alternatives. Luckly I found a way to get the device running as described below.

### Option 1 - Android App

Without even a device identification, I digging in internet to found a bizarre MS Word document with few and messy instructions about how to control the board. According this document, it is required to use a crapy android app to send AT commands to the ESP in order to make the relay switch. This way wasn't an option for me since I wont to apply the board into a home automatation MQTT environment.

### Option 2 - Physically change the board

Searching a bit more, I found another post instructions explaining how to remove resistors and soudering jumps in order to control the relay "in regular fashion" by the PIN 2 bypassing the STC 15f104W. Again not an option for me since I don't like to change constructive devices features in so intrusive way for real applications.

### Option 3 - The solution

Reading the MS word document again I realized that the strings sent by the Android Application to the ESP (into AT commands) were basically forward through the RXTX interface to the board. To check my finds I wrote a straightforward application just to ESP send these strings to the board:

```
void setup() {
  Serial.begin(9600);
}

void loop() {
  byte close[] = {0xA0, 0x01, 0x01, 0xA2};
  Serial.write(close, sizeof(close)); 
  delay(2000); 
  
  byte open[] = {0xA0, 0x01, 0x00, 0xA1};
  Serial.write(open, sizeof(open)); 
  delay(2000);      
}
```
After uploaded the code to the ESP and plug it in the board the relay began to switch into the two states every 2 seconds. Gotcha!

Almost done, now let's plug it in the MQTT bus.

## Making the board a MQTT subscriber

If you are familiar with MQTT platform, you know that each device can act as a publisher or a subscriber (or both). A publisher is something that inserts new messages into the bus and a subscriber consome these messages. So in this architecture the board can act as a subscriber, receiving messages from the MQTT Broker. Thus, all messages sent for a specific 'topic' will be delivered to the board. For example, if the board subscribe to the topic "/myRelay-001/command", a message published by another MQTT client like:

```
$ mosquitto_pub -t "/myRelay-001/command" -u "myuser" -P "mypassword" -m "CLOSE"
```
will be delivered to the board. So, the remain task is to write a subscriber MQTT client to receive that kind of message and responding in according (opening/closing).

## Coding the MQTT subscriber

The complete code can be found here in this repository. Anyway, the main points are:

### Use the right headers

Here you will need the both PubSubClient and the Esp8266 Wifi API. Look at the bottom of this document if you need assistance about how to configure the Esp or the MQTT libraries.

```
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
```

### Create the client

MQTT protocol runs in the top of the application layer of the TCP/IP network. you need a working Wifi network and credentials.

```
WiFiClient wifiClient;
PubSubClient client(wifiClient);
```

### Write the setup and loop

The setup is pretty simple. Take attention to the Serial baudrate. It must be 9600 to the board understand what the ESP is sending.

```
  Serial.begin(9600);
  delay(10);
  Serial.println("Let' start now");
```
The loop just verify the connection status and reconnects if necessary. Do not forget the ```client.loop()``` line otherwise your MQTT subscriber client will not be notified!

```
  if ( !client.connected() ) {
    connect();
  }
  client.loop();
```
### Connect as subscriber

Here the most important aspects are the line ```client.subscribe(topic);``` which define the role of the client in the MQTT protocol as a subscribe; and the line ```client.setCallback(callback);``` that defines which function will be called on message arrives.

```
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
```

### Write the callback function

This is the callback function. The main task is unwrap the payload and check if is a OPEN or CLOSE operation. The code is pretty straighforward but one point of relevance must be highligted: you should to avoid consecutives OPEN-CLOSE operation in short time. Lets imagine that hundred of published messages arrives in the device every second. In this scenario the relay should be hundred of Open-Close operations in a short time. This scenario is dangerous since in each open operation is generate heat from the dissipation of the electrical current. The consecutives open-close cuycles can generate heat and start a fire. So, to avoi it, after each operation is stored the timestamp. Another operation is performed just in case the delta time is acceptable (the MIN_OPERATION_INTERVAL constant).

```
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
```
## Troubleshooting

Something in my bowels says that this troubleshooting section should be long. But I'll try to be short.

### Power Supply

After programming the ESP you should to deploy it into the board and power up the board by a 5V reliable power source. In my tests I used a 2 Amp. The Esp itself drain energy and the Relay need enery as well to perform its mechanical operation. I don't tried to power the board just with the energy from the USB and I guess it is a bad idea even for test purposes.
So, if the relay do not open/close one good shoot could be to check the power source.

### Cables

Every connection must be revised BEFORE you turn on your Power Supply. In some cases, like the TX-RX connection, inverted cables are just a temporary inconvenient: after you realized that the cables are switched just fix the connection and go on. But if you do a mistake with VCC and ground cables you mostly will damage your device seriously. So, take a breath and review your connection soonner.

![power supply](https://raw.githubusercontent.com/doleron/esp8266-1-channel-relay-board-with-mqtt/master/images/powersupply.JPG)

The power supply I have used. 

Remember to review your connections BEFORE turn up the power. You have been warned.

### The right serial baudrate

Is 9600. If you don't meet it the board will not work. And there no way to change this setting.

### My code doesn't work when I plug the Esp into the board - part 1

If everuthing is right (WIFI and MQTT credentials for example) than the way to go is to check the ESP in operation inside the board. A nice thing in this board is 4 pins for VCC, GND, RX and TX. If you what to see the serial output from the ESP (and maybe error messages) you can connect your FTDI with the board: GND-GND, TX-RX, RX-TX. DISCLAIMER: DO NOT CONNECT THE VCC-VCC or your USB port can be damaged.

### My code doesn't work when I plug the Esp into the board - part 2

Yes it is crazy but this board has a severe design issue: the ESP's Antenna is strongly atennuated by the board become unsable. Yes, when you plug the ESP in the board the ESP antenna doesn't work and the ESP cannot connect to the WIFI. it is crazy I know but I just brought the board, I didn't designed it. My workaround was to improvise a secondary antenna. Just connect a free jump in the RX connector and the ESP will be able to connect to your WIFI again.

![improvised antenna](https://raw.githubusercontent.com/doleron/esp8266-1-channel-relay-board-with-mqtt/master/images/improvised_antenna.JPG)

An improvised antenna just in case if you have a hard day like my one today

## Acknowledgment

I like to say thanks to the people on China whom have design and build so lovely device.


