/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"


// ==================== 常量定义 ====================




// ==================== 全局变量 ====================
String deviceName;           // 设备名称
String gpsCoordinates;       // GPS坐标信息
char sendData[BUFFER_SIZE];  // 发送数据缓存
RadioEvents_t radioEvents;   // LoRa事件回调
int packetCount = 0;         // 数据包计数器

String displayBuf[4] = { "", "", "", "" };

// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）




// LoRa接收窗口状态
bool inRxMode = false;           // 当前是否处于接收模式
unsigned long rxStartTime = 0;   // RX窗口开始的millis()
bool didSend = false;            // 本周期是否已发送（控制RX窗口和休眠）
bool rxWindowDone = false;       // 本周期RX窗口是否已结束（防止收到消息后重新进入）
char rxBuffer[BUFFER_SIZE + 1];  // 接收数据缓存
bool oledOpen = true;

// ==================== 计算下次发送时间 (修正版) ====================
unsigned long calculateNextSendTime(unsigned long intervalSeconds) {
  if (deviceIndex < 0 || totalDevices == 0) {
    deviceIndex = getDevicesIdx();
    totalDevices = getTotalDevices();
    Serial.printf("设备索引: %d, 总设备数: %d\n", deviceIndex, totalDevices);
  }
  if (deviceIndex < 0 || totalDevices <= 0) {
    Serial.println("⚠️ 设备未认证，使用默认间隔");
    return millis() + intervalSeconds * 1000;
  }

  // 1. 获取当前时间
  String timeStr = getCurrentTime();
  int hour = 0, minute = 0, second = 0;
  sscanf(timeStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &hour, &minute, &second);
  unsigned long currentSeconds = hour * 3600 + minute * 60 + second;

  // 2. 计算基础参数

  unsigned long mySlotOffset = (unsigned long)(deviceIndex * slotDuration);  // 我在周期内的偏移量

  // 3. 核心修复逻辑：计算到下一个时隙的等待时间
  // 计算从当天0点开始，已经经历了多少个完整的周期
  unsigned long cyclesPassed = currentSeconds / intervalSeconds;

  // 计算“上一个”属于我的发送时隙的绝对时间点
  unsigned long lastTargetSeconds = cyclesPassed * intervalSeconds + mySlotOffset;

  long secondsDiff = 0;

  // 判断“上一个”时隙是否已经过去
  if (lastTargetSeconds < currentSeconds) {
    // 如果已过，那么下一个时隙就是再等一个完整的周期
    secondsDiff = intervalSeconds - (currentSeconds - lastTargetSeconds);
  } else {
    // 如果还没到（说明当前时间正好在周期的最开始部分），下一个时隙就是它
    secondsDiff = lastTargetSeconds - currentSeconds;
  }

  // 确保等待时间不为负数
  if (secondsDiff < 0) {
    secondsDiff += intervalSeconds;
  }

  unsigned long delayMillis = secondsDiff * 1000;
  Serial.printf("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu ms\n", timeStr.c_str(), deviceIndex, slotDuration, delayMillis);
  return millis() + delayMillis;
}


// ==================== LoRa模块初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = onRxDone;
  radioEvents.RxTimeout = onRxTimeout;
  initPanRadio(&radioEvents);
}
// ==================== 读取电池电量 ====================
String readBatteryLevel() {
  // V4 与 V3 控制逻辑相反：V3 LOW 开启，V4 HIGH 开启
  bool isV4 = deviceName.startsWith("v4-");
  digitalWrite(VBAT_CTRL_PIN, isV4 ? HIGH : LOW);
  delay(100);

  const int samples = 10;
  long rawSum = 0;
  long mvSum = 0;
  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(VBAT_READ_PIN);
    mvSum += analogReadMilliVolts(VBAT_READ_PIN);
    delay(1);
  }
  float rawAvg = (float)rawSum / samples;
  float mvAvg = (float)mvSum / samples;

  // 关闭检测电路（V3 HIGH 关闭，V4 LOW 关闭）
  digitalWrite(VBAT_CTRL_PIN, isV4 ? LOW : HIGH);

  // 分压系数 5.35（实测校准：785mV × 5.35 ≈ 4.2V 满电）
  float batteryVoltage = mvAvg * 5.35 / 1000.0;

  Serial.printf("[BAT] raw=%.0f mv=%.0f V=%.2f\n", rawAvg, mvAvg, batteryVoltage);

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 100);
  soc = constrain(soc, 0, 100);
  float socRatio = soc / 100.0;

  // 格式: soc|adc_raw|adc_mV|voltage
  return String(socRatio, 2) + "|" + String((int)rawAvg) + "|" + String((int)mvAvg) + "|" + String(batteryVoltage, 2);
}

// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {
  packetCount++;
  String dataStr = String(packetType) + "|" + deviceName;

  if (packetType == MSG_TYPE_GPS) {
    gpsCoordinates = getGpsInfoStr();
    displayBuf[2] = gpsCoordinates;
    dataStr += "|" + gpsCoordinates + "|" + String(packetCount);
  } else if (packetType == MSG_TYPE_TIME) {
    dataStr += "|" + getCurrentTime() + "|" + String(packetCount);
  } else if (packetType == MSG_TYPE_BATTERY) {
    dataStr += "|" + readBatteryLevel() + "|" + String(packetCount);
  }

  // 安全拷贝到发送缓冲区
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }

  // 打印发送信息
  Serial.print("发送：");
  Serial.print(sendData);
  Serial.print("  len:");
  Serial.println(strlen(sendData));

  // 执行LoRa发送
  Radio.Send((uint8_t*)sendData, strlen(sendData));
  delay(100);
}
unsigned long starQuickSendTime = 0;  // 下次发送时间点（millis）
bool isQuickModel = false;
void testQuick(unsigned long intervalSec) {
  if (millis() > (starQuickSendTime + SEND_INTERVAL_MS)) {
    isQuickModel = false;
    return;
  }

  // 1. 获取当前时间
  String timeStr = getCurrentTime();
  int hour = 0, minute = 0, second = 0;
  sscanf(timeStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &hour, &minute, &second);
  unsigned long currentSeconds = hour * 3600 + minute * 60 + second;

  unsigned long cyclesPassed = currentSeconds / intervalSec;
  unsigned long passtm = currentSeconds - (cyclesPassed * intervalSec);
  unsigned long mySlotOffset = (unsigned long)(deviceIndex * slotDuration);  // 我在周期内的偏移量
  unsigned long idx = passtm / (totalDevices * slotDuration);
  if (idx < 1) {
    idx = 1;
  }
  unsigned long nextTm = (idx * (totalDevices * slotDuration)) + mySlotOffset;
  if (nextTm < passtm) {
    nextTm += (totalDevices * slotDuration);
  }

  Serial.print(passtm);
  Serial.print(" ");
  Serial.print(mySlotOffset);
  Serial.print(" ");
  Serial.print(nextTm);
  Serial.print(" ");
  Serial.println(idx);
  if (nextTm == passtm) {
    Serial.println("快速发关一条消息 ");
    buildAndSendPacket(MSG_TYPE_BATTERY);
  }

  delay(1000);
}

// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  Radio.Sleep();
  // Serial.println("✅ 发送完成");
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {
  Radio.Sleep();
  Serial.println("❌ 发送超时");
}

// ==================== LoRa接收完成回调 ====================
void onRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  Radio.Sleep();
  // 不设inRxMode=false，由loop()超时检查统一处理退出和打印
  int copyLen = (size < BUFFER_SIZE) ? size : BUFFER_SIZE;
  memcpy(rxBuffer, payload, copyLen);
  rxBuffer[copyLen] = '\0';

  Serial.printf("📨 收到LoRa数据 [%d字节] RSSI:%d SNR:%d\n", size, rssi, snr);
  Serial.printf("   内容: %s\n", rxBuffer);

  // 解析消息类型，处理对时消息
  String rxStr = String(rxBuffer);
  int typeEnd = rxStr.indexOf('|');
  if (typeEnd > 0) {
    int secondPipe = rxStr.indexOf('|', typeEnd + 1);
    int msgType = rxStr.substring(0, typeEnd).toInt();
    if (secondPipe > 0) {
      if (msgType == MSG_TYPE_TIME) {
        String timeStr = rxStr.substring(secondPipe + 1);
        setTimeFromLora(timeStr);
      } else if (msgType == MSG_TYPE_COM) {
        String result = rxStr.substring(typeEnd + 1, secondPipe);
        if (result == deviceName) {
          int lastValue = rxStr.substring(secondPipe + 1).toInt();
          // 打印验证一下
          Serial.print("提取到的最后整数是: ");
          Serial.println(lastValue);
          // 如果lastValue为1，启动快速发送模式
          if (lastValue == 1) {
            starQuickSendTime = millis();  // 下次发送时间点（millis）
            isQuickModel = true;

          } else if (lastValue == 4) {
            Serial.println("⚠️⚠️⚠️⚠️ 收到重启指令，系统将在 1 秒后重启...");
            delay(1000);    // 强烈建议加一个短暂的延时，确保串口日志能发送出去
            ESP.restart();  // 执行重启

          } else if (lastValue == 5) {
            //只有GPS打开时才可以使用led
            if (getGpsStatus() == true) {
              oledOpen = !oledOpen;
              if (oledOpen) {
                Serial.println("⚠️⚠️⚠️⚠️打开OLED⚠️⚠️⚠️⚠️");
                showOLED();
              } else {
                Serial.println("⚠️⚠️⚠️⚠️关闭OLED⚠️⚠️⚠️⚠️");
                hideOLED();
              }
            }

          } else {
            Serial.println("❌❌❌❌❌❌这是专门为这对设备下发的指令，请及时补充功能❌❌❌❌❌❌， ");
          }
        }
      }
    }
  }
  displayBuf[3] = "RX:" + String(copyLen) + "B";
}

// ==================== LoRa接收超时回调 ====================
void onRxTimeout(void) {
  Radio.Sleep();
  inRxMode = false;
  Serial.println("⏹ RX超时，结束接收... " + getCurrentTime());
}

// ==================== 进入接收窗口 ====================
void startRxWindow() {
  isQuickModel = false;
  inRxMode = true;
  rxWindowDone = true;
  rxStartTime = millis();
  Serial.println("📡 进入接收窗口... " + getCurrentTime());
  displayBuf[3] = "RX Listening";
  Radio.Rx(0);  // 连续接收模式
}
// ==================== 系统初始化 ====================
void setup() {
  delay(1000);
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  analogReadResolution(12);
  pinMode(VBAT_CTRL_PIN, OUTPUT);
  digitalWrite(VBAT_CTRL_PIN, HIGH);

  // 生成设备名称并初始化显示
  deviceName = makeDivceName();
  displayBuf[0] = "Device: " + deviceName;
  displayBuf[1] = "";
  displayBuf[2] = "Waiting GPS...";
  displayBuf[3] = "LoRa Ready";

  // 初始化LoRa模块
  initLora();
  initPanGPS();
}
// ==================== 主循环 ====================
void loop() {
  delay(10);
  gpsEncode();
  unsigned long intervalSec = SEND_INTERVAL_MS / 1000;
  bool canRx = (intervalSec > RX_WINDOW_SECONDS + 1);
  unsigned long rxWindowMs = canRx ? RX_WINDOW_SECONDS * 1000 : 0;

  // 首次运行：计算第一次发送时间
  if (nextSendTime == 0) {
    nextSendTime = calculateNextSendTime(intervalSec);
  }


  if (isQuickModel) {
    testQuick(intervalSec);
  }


  // ====== 正常模式：RX窗口5秒超时检查 ======
  if (inRxMode && (millis() - rxStartTime >= rxWindowMs)) {
    Radio.Sleep();
    inRxMode = false;
    Serial.println("⏹ 结束接收窗口... " + getCurrentTime());
  }

  //提前开启GPS先设定60秒
  if ((millis() >= (nextSendTime - 60000)) && millis() < nextSendTime) {
    // Serial.println("开起GPS窗口");
    setGpsEnable(true);
  }
  // ====== 阶段1：到达发送时间，执行发送 ======
  if (millis() >= nextSendTime) {
    // 如果正在接收，先退出RX模式
    if (inRxMode) {
      Radio.Sleep();
      inRxMode = false;
      Serial.println("⏹ 结束接收窗口... " + getCurrentTime());
    }

    didSend = true;
    rxWindowDone = false;  // 新周期重置RX窗口标记


    // int packetType = random(2) == 0 ? MSG_TYPE_GPS : MSG_TYPE_TIME;MSG_TYPE_BATTERY
    const int typeList[] = { MSG_TYPE_GPS, MSG_TYPE_TIME, MSG_TYPE_BATTERY };
    int packetType = typeList[packetCount % 3];

    unsigned long aaa = millis();
    buildAndSendPacket(MSG_TYPE_BATTERY);
 
    setGpsEnable(false);

    openLedByNum(10, 50);
    displayBuf[3] = "Sending...";
    displayBuf[0] = "id:  " + deviceName + "  " + packetCount;
    displayBuf[1] = getCurrentTime();
    showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);

    // 计算下一次发送时间
    nextSendTime = calculateNextSendTime(intervalSec);
    return;
  }

  // ====== 阶段2：接收窗口（周期最后RX_WINDOW_SECONDS秒开启接收） ======
  if (didSend && canRx && !inRxMode && !rxWindowDone) {
    // 基于当前时间计算周期边界，RX窗口对齐到周期最后5秒
    String timeNow = getCurrentTime();
    int h, m, s;
    sscanf(timeNow.c_str(), "%*d/%*d/%*d %d:%d:%d", &h, &m, &s);
    unsigned long timeOfDaySec = h * 3600UL + m * 60UL + s;
    unsigned long nextCycleBoundaryMs = millis() + (intervalSec - timeOfDaySec % intervalSec) * 1000UL;
    unsigned long rxStartMs = nextCycleBoundaryMs - rxWindowMs;
    if (millis() >= rxStartMs && millis() < nextCycleBoundaryMs) {
      startRxWindow();
    }
  }

  // ====== 阶段3：非发送、非接收时间，休眠等待 ======
  if (didSend && !inRxMode) {
    unsigned long remaining = (nextSendTime - millis()) / 1000;
    displayBuf[3] = "Sleep " + String(remaining) + "s";
  }

  // 处理LoRa中断
  Radio.IrqProcess();

  // 更新OLED显示
  displayBuf[0] = "id:  " + deviceName + "  " + packetCount;
  displayBuf[1] = getCurrentTime();
  showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);
}
