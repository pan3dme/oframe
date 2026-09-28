#include <SoftwareSerial.h>
#include <TinyGPS++.h>

SoftwareSerial gpsSerial(5, 6);
TinyGPSPlus gps;

#define GPS_BUT9 9  // GPS天线电源使能
// 工作状态
enum DevState {
  GPS_SEARCH,  // 搜索卫星
  GPS_SLEEP    // 休眠倒计时
};
DevState devState = GPS_SEARCH;
unsigned long sleepStartTick = 0;
const unsigned long SLEEP_TIME = 20 * 1000;  // 20秒

void setup() {
  Serial.begin(9600);
  gpsSerial.begin(9600);

  pinMode(GPS_BUT9, OUTPUT);
}

int skipnum = 0;

void readGpsInfo() {
  Serial.print("GPS Buffer Available: ");
  Serial.println(gpsSerial.available());
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.print("===== GPS信息 =====");
    Serial.print("定位状态：");
    Serial.print(gps.location.isValid() ? "有效(A)" : "无效(V)");
    if (gps.location.isValid()) {
      Serial.print(" 纬度: ");
      Serial.print(gps.location.lat(), 6);
      Serial.print("  经度: ");
      Serial.print(gps.location.lng(), 6);
    } else {
      Serial.print(" 纬度: 无  经度: 无");
    }
    Serial.print(" 卫星数量：");
    Serial.print(gps.satellites.value());
    Serial.print(" UTC时间：");
    if (gps.time.isValid()) {
      Serial.print(gps.time.hour());
      Serial.print(":");
      Serial.print(gps.time.minute());
      Serial.print(":");
      Serial.print(gps.time.second());
    } else {
      Serial.print("0.00000,0.00000");
    }
    Serial.print(" HDOP精度因子：");
    Serial.print(gps.hdop.value() / 10.0);
    Serial.println();
  }
}

void loop() {
  digitalWrite(GPS_BUT9, HIGH);
  if (devState == GPS_SEARCH) {
    // 搜星模式
    // digitalWrite(13, LOW);

    readGpsInfo();

    // GPS定位成功，切到20秒休眠倒计时
    if (gps.location.isValid()) {
      // Serial.println("✅ GPS定位成功，进入20秒休眠倒计时，暂停搜星");
      // devState = GPS_SLEEP;
      // sleepStartTick = millis();
      // gps = TinyGPSPlus();  // 清空GPS旧数据
    }

    delay(500);
    // digitalWrite(13, HIGH);
    // digitalWrite(9, LOW);
    delay(500);
    Serial.print("skip:");
    Serial.print(skipnum++);
  } else if (devState == GPS_SLEEP) {
    // 休眠倒计时，不读取GPS数据
    unsigned long past = millis() - sleepStartTick;
    if (past >= SLEEP_TIME) {
      Serial.println("⏰ 20秒倒计时结束，重新开启GPS搜星");
      devState = GPS_SEARCH;
      skipnum = 0;
    } else {
      // LED继续闪烁
      // digitalWrite(13, LOW);

      delay(500);
      // digitalWrite(13, HIGH);

      delay(500);
      Serial.print("休眠剩余秒数:");
      Serial.print((SLEEP_TIME - past) / 1000);
      Serial.print(" | skip:");
      Serial.println(skipnum++);
    }
  }
}
