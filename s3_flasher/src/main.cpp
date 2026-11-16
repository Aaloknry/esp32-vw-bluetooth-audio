#include <Arduino.h>

#define WROOM_TX_PIN 18 
#define WROOM_RX_PIN 17 

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(115200);

  Serial1.setRxBufferSize(16384);
  Serial1.begin(115200, SERIAL_8N1, WROOM_TX_PIN, WROOM_RX_PIN);
}

void loop() {
  while (Serial.available()) {
    Serial1.write(Serial.read());
  }
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}
