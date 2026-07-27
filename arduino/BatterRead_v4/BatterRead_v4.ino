/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <pan3dme.h>

// ==================== 全局变量 ====================


String batterystr = "";
// LoRa事件回调


String readBatteryEndStrCopy(String deviceName) {
  analogReadResolution(12);
  pinMode(VBAT_CTRL_PIN, OUTPUT);

  // V4 与 V3 控制逻辑相反：V3 LOW 开启，V4 HIGH 开启
  bool isV4 = deviceName.startsWith("v4-");
  digitalWrite(VBAT_CTRL_PIN, isV4 ? HIGH : LOW);
  delay(10);

  const int samples = 10;
  long rawSum = 0;
  long mvSum = 0;
  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(VBAT_READ_PIN);
    mvSum += analogReadMilliVolts(VBAT_READ_PIN);
    delay(10);
  }
  float rawAvg = (float)rawSum / samples;
  float mvAvg = (float)mvSum / samples;

  digitalWrite(VBAT_CTRL_PIN, isV4 ? LOW : HIGH);
  delay(10);
  pinMode(VBAT_CTRL_PIN, INPUT_PULLDOWN);

  float batteryVoltage = mvAvg * 5.35 / 1000.0;

  Serial.printf("[BAT] raw=%.0f mv=%.0f V=%.2f\n", rawAvg, mvAvg,
                batteryVoltage);

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 100);
  soc = constrain(soc, 0, 100);
  float socRatio = soc / 100.0;

  String outStr = String(socRatio, 1) + "|" + String(batteryVoltage, 1);
  Serial.print("电量信息：");
  Serial.println(outStr);



  String socStr = String(batteryVoltage, 2) + "V   " + String(soc) + "%";
  String mvStr = "ADC:" + String((int)mvAvg) + "mV";
  String rawStr = "Raw:" + String((int)rawAvg);
  showDisplayBy4Area(deviceName, socStr, mvStr, rawStr);

  return outStr;
}
String deviceName = "";
// ==================== 系统初始化 ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.print("setup");
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  randomSeed(analogRead(0));

  deviceName = makeDivceName();
}

// ==================== 主循环 ====================
void loop() {
  batterystr = readBatteryEndStrCopy(deviceName);

  delay(1000);
}