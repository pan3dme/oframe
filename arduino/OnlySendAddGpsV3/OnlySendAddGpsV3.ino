#define GPS_RX_PIN 18
#define GPS_TX_PIN 17

bool isSleeping = false;
bool gpsLowPower = false;
const unsigned long WORK_INTERVAL_MS = 30000;
const unsigned long SLEEP_INTERVAL_MS = 30000;
unsigned long stateStartMillis = 0;

#include "HT_TinyGPS++.h"
TinyGPSPlus gps;

// UBX 电源管理指令（你原来的）
const byte disableAuto[]  = {0x62,0x06,0x3C,0x02,0x00,0x00,0x00,0x3E,0x31};
// 休眠 Backup 模式
const byte sleepCmd[] = {0x62,0x06,0x3C,0x02,0x00,0x01,0x01,0x40,0x33};
// 唤醒 正常模式
const byte wakeCmd[]  = {0x62,0x06,0x3C,0x02,0x00,0x01,0x01,0x40,0x33};

// 关键：检测GPS射频是否还在工作
bool isGpsRfOn()
{
  static unsigned long checkStart = 0;
  static bool foundNmea = false;

  // 每 1 秒检测一次
  if (millis() - checkStart > 1000)
  {
    checkStart = millis();
    foundNmea = false;

    // 读取1秒内所有数据，看有没有 $GP / $GN
    while (Serial2.available())
    {
      char c = Serial2.read();
      if (c == '$')
      {
        // 再读2个字符，判断是不是 GP/GN 开头
        if (Serial2.available() >= 2)
        {
          char t1 = Serial2.read();
          char t2 = Serial2.read();
          if ((t1 == 'G' && t2 == 'P') || (t1 == 'G' && t2 == 'N'))
          {
            foundNmea = true;
          }
        }
      }
    }
  }
  return foundNmea;
}

void gpsSleep()
{
  Serial2.write(sleepCmd, sizeof(sleepCmd));
  delay(200);
  isSleeping = true;
  gpsLowPower = true;
  Serial.println("[INFO] 已下发休眠指令");
}

void gpsWake()
{
  Serial2.write(wakeCmd, sizeof(wakeCmd));
  delay(2500);
  isSleeping = false;
  gpsLowPower = false;
  Serial.println("[INFO] 已下发唤醒指令");
}

void setup()
{
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(1000);

  Serial2.write(disableAuto, sizeof(disableAuto));
  delay(150);

  Serial.println("===== GPS 初始化完成 =====");
  stateStartMillis = millis();
  isSleeping = false;
  gpsLowPower = false;
}

void loop()
{
  static unsigned long lastPrint = 0;
  unsigned long now = millis();

  // 工作态解析NMEA；休眠态清空
  if (!isSleeping)
  {
    while (Serial2.available())
    {
      gps.encode(Serial2.read());
    }
  }
  else
  {
    while (Serial2.available())
    {
      Serial2.read();
    }
  }

  // 每秒打印：工作/休眠 + 射频状态 + 卫星数 + 倒计时
  if (now - lastPrint >= 1000)
  {
    lastPrint = now;
    unsigned long elapsed = now - stateStartMillis;
    unsigned long remaining = isSleeping ?
      ((elapsed >= SLEEP_INTERVAL_MS) ? 0 : (SLEEP_INTERVAL_MS - elapsed)) :
      ((elapsed >= WORK_INTERVAL_MS) ? 0 : (WORK_INTERVAL_MS - elapsed));

    bool rfOn = isGpsRfOn();

    if (isSleeping)
    {
      Serial.print("[SLEEP] 射频：");
      Serial.print(rfOn ? "❌ 未关闭（异常）" : "✅ 已关闭（正常）");
      Serial.print(" | 倒计时：");
      Serial.print(remaining / 1000);
      Serial.println("s 后唤醒");
    }
    else
    {
      Serial.print("[WORK] 射频：✅ 开启 | 卫星：");
      Serial.print(gps.satellites.value());
      Serial.print(" | 倒计时：");
      Serial.print(remaining / 1000);
      Serial.println("s 后休眠");
    }
  }

  // 定时切换
  if (!isSleeping && (now - stateStartMillis >= WORK_INTERVAL_MS))
  {
    gpsSleep();
    stateStartMillis = now;
  }
  if (isSleeping && (now - stateStartMillis >= SLEEP_INTERVAL_MS))
  {
    gpsWake();
    stateStartMillis = now;
  }
}