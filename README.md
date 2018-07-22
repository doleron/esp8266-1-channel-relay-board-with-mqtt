# esp8266-01 1-Channel relay board with mqtt

Sketch to control an esp8266-01 STC 15f104W powered 1-channel relay board to be controlled remotely using a MQTT broker as communication bus.

## The board

I bought this chinese board in a local retailer and the device was delivered whitout any kind of technical instructions. It have a single channel 10 Amp Relay and a slot to plug an Esp8266 into. Besides voltage regulators and other passive components, the board have a CHIP STC 15f104W with is responsible for receive commands from the esp and directly command the relay.

## Trying to use the board

### Option 1 - Android App

Digging in internet I found a bizarre MS Word document with messy instructions about how to control the board. According with this document, it is required to use a crapy android app to send AT commands to the ESP. Regarding the bad smell, this way wasn't an option for me since I wont to apply the board into a home automatation MQTT environment.

### Option 2 - physically change the board

Searching a bit more, I found another post instructions how to remove resistors and soudering jumps in order to control the relay "in regular fashion" by the PIN 2 bypassing the STC 15f104W. Again not an option for me since I don't like to change constructive features of the devices so intrusive in real applications.

### Option 3 - the solution

Reading the MS word document again I realized that the strings sent by the Android Application to the ESP into AT commands were basically forward through the RXTX interface to the board. To check my finds I wrote a straightforward application just to send these string:

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
After uploaded the code to the ESP and plug it in the board the relay began to switch into the two states. Gotcha!

Almost done, now let's plug it in the MQTT bus.

## Making the board a MQTT subscriber

If you are familiar with MQTT platform, you know that each device can act as a publisher or a subscriber (or both). A publisher is something that inserts new message on the bus and a subscriber consome these messages. So in this architecture the board can act as a subscriber, receiving messages from the MQTT Broker. Thus, all messages sent for a specific 'topic' will be delivered to the board. For example, if the device subscribe to the topic "/myRelay-001/command", a message published by another MQTT client like:

```
$ mosquitto_pub -t "/myRelay-001/command" -u "myuser" -P "mypassword" -m "CLOSE"
```
will be delivered to the board. So, the remain task is to write a subscriber MQTT client to receive that kind of message and responding in according (opening/closing).

## Coding the MQTT subscriber

The complete code can be found here in this repository. Anyway, the main points are:

Use the right headers:
```
```

Create the client:
```
```

Connect as subscriber
```
```

Write the callback function:
```
```




