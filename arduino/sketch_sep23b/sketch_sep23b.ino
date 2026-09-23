
#include <SoftwareSerial.h>
#include <TinyGPS++.h>



SoftwareSerial gpsSerial(2, 3);
TinyGPSPlus gps;
void setup() {
  Serial.begin(9600);
  gpsSerial.begin(9600);
  pinMode(13, OUTPUT);
  pinMode(9, OUTPUT);
}
int skipnum = 0;
void readGpsInfo() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // 每秒打印一次信息
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();

    Serial.println("===== GPS信息 =====");
    Serial.print("定位状态：");
    Serial.println(gps.location.isValid() ? "有效(A)" : "无效(V)");

    if (gps.location.isValid()) {
      Serial.print("纬度: ");
      Serial.print(gps.location.lat(), 6);
      Serial.print("  经度: ");
      Serial.println(gps.location.lng(), 6);
    } else {
      Serial.println("纬度: 无  经度: 无");
    }

    Serial.print("卫星数量：");
    Serial.println(gps.satellites.value());

    Serial.print("UTC时间：");
    if (gps.time.isValid()) {
      Serial.print(gps.time.hour());
      Serial.print(":");
      Serial.print(gps.time.minute());
      Serial.print(":");
      Serial.println(gps.time.second());
    } else {
      Serial.println("无");
    }

    Serial.print("HDOP精度因子：");
    Serial.println(gps.hdop.value() / 10.0);
    Serial.println();
  }
}
void loop() {
  digitalWrite(13, LOW);
  digitalWrite(9, HIGH);
  delay(500);  // 亮1秒
  digitalWrite(13, HIGH);
  digitalWrite(9, LOW);
  delay(500);  // 灭1秒
  readGpsInfo();
  Serial.print("skip:");
  Serial.println(skipnum++);
}