#include <SoftwareSerial.h>
SoftwareSerial mySerial(5, 6);

void setup() {
  Serial.begin(115200);
  mySerial.begin(115200);
  Serial.println("===== 开始监听 STM32 USART1 =====");
}

void loop() {
  if (mySerial.available()) {
    Serial.write(mySerial.read());
  }
}