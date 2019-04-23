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