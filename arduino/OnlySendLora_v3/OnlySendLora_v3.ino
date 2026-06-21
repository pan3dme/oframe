/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"

// ==================== 电池检测引脚 ====================

// ==================== 常量定义 ====================
const char* DEVICE_NAME_PREFIX = "v4-x";
const unsigned long RX_WINDOW_SECONDS = 5;  // 接收窗口秒数（周期最后N秒用于接收）


// ==================== 全局变量 ====================
String deviceName;           // 设备名称
String gpsCoordinates;       // GPS坐标信息
char sendData[BUFFER_SIZE];  // 发送数据缓存
RadioEvents_t radioEvents;   // LoRa事件回调
int packetCount = 0;         // 数据包计数器
String displayLines[4];      // OLED显示内容

// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）

// LoRa接收窗口状态
bool inRxMode = false;           // 当前是否处于接收模式
unsigned long rxStartTime = 0;   // RX窗口开始的millis()
bool didSend = false;            // 本周期是否已发送（控制RX窗口和休眠）
char rxBuffer[BUFFER_SIZE + 1];  // 接收数据缓存

// ==================== 计算下次发送时间 ====================
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

  // 通过getCurrentTime()获取当前时间（已统一所有时间源优先级）
  String timeStr = getCurrentTime();
  int hour = 0, minute = 0, second = 0;
  sscanf(timeStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &hour, &minute, &second);

  // 基于当前时间计算时隙
  unsigned long currentSeconds = hour * 3600 + minute * 60 + second;
  float slotDuration = (float)intervalSeconds / totalDevices;
  float targetSecondInCycle = deviceIndex * slotDuration;
  unsigned long cycleCount = currentSeconds / intervalSeconds;
  unsigned long targetSeconds = cycleCount * intervalSeconds + (unsigned long)targetSecondInCycle;

  if (targetSeconds <= currentSeconds) {
    targetSeconds = (cycleCount + 1) * intervalSeconds + (unsigned long)targetSecondInCycle;
  }

  if (targetSeconds >= 86400) {
    targetSeconds -= 86400;
  }

  long secondsDiff = (long)targetSeconds - (long)currentSeconds;
  unsigned long delayMillis = secondsDiff * 1000;

  Serial.printf("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu ms\n",
                timeStr.c_str(), deviceIndex, slotDuration, delayMillis);

  return millis() + delayMillis;
}

// ==================== 读取GPS信息 ======================================
void updateGpsInfo() {
  gpsCoordinates = getGpsInfoStr();
  displayLines[2] = gpsCoordinates;
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
  digitalWrite(VBAT_CTRL_PIN, LOW);
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

  digitalWrite(VBAT_CTRL_PIN, HIGH);

  // 分压系数 5.35（实测校准：785mV × 5.35 ≈ 4.2V 满电）
  float batteryVoltage = mvAvg * 5.35 / 1000.0;

  Serial.printf("[BAT] raw=%.0f mv=%.0f V=%.2f\n", rawAvg, mvAvg, batteryVoltage);

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 100);
  soc = constrain(soc, 0, 100);
  float socRatio = soc / 100.0;

  // 格式: soc|adc_raw|adc_mV|voltage
  return String(socRatio, 2) + "|" + String((int)rawAvg) + "|" + String((int)mvAvg) + "|" + String(batteryVoltage, 2);
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
  displayLines[0] = "Device: " + deviceName;
  displayLines[1] = "";
  displayLines[2] = "Waiting GPS...";
  displayLines[3] = "LoRa Ready";

  // 初始化LoRa模块
  initLora();
  initPanGPS();
}
// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {
  String dataStr = String(packetType) + "|" + deviceName;

  if (packetType == MSG_TYPE_GPS) {
    updateGpsInfo();
    dataStr += "|" + gpsCoordinates + "|" + String(packetCount);
  } else if (packetType == MSG_TYPE_TIME) {
    dataStr += "|" + getCurrentTime();
  } else if (packetType == MSG_TYPE_BATTERY) {
    dataStr += "|" + readBatteryLevel();
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
}
// ==================== 主循环 ====================
void loop() {
  delay(1);
  gpsEncode();

  unsigned long currentMs = millis();
  unsigned long intervalSec = SEND_INTERVAL_MS / 1000;
  bool canRx = (intervalSec > RX_WINDOW_SECONDS + 1);
  unsigned long rxWindowMs = canRx ? RX_WINDOW_SECONDS * 1000 : 0;

  // 首次运行：计算第一次发送时间
  if (nextSendTime == 0) {
    nextSendTime = calculateNextSendTime(intervalSec);
  }

  // RX窗口5秒超时检查（独立于发送时间）
  if (inRxMode && (currentMs - rxStartTime >= rxWindowMs)) {
    Radio.Sleep();
    inRxMode = false;
    Serial.println("⏹ 结束接收窗口... " + getCurrentTime());
  }

  // ====== 阶段1：到达发送时间，执行发送 ======
  if (currentMs >= nextSendTime) {
    // 如果正在接收，先退出RX模式
    if (inRxMode) {
      Radio.Sleep();
      inRxMode = false;
      Serial.println("⏹ 结束接收窗口... " + getCurrentTime());
    }

    didSend = true;
    packetCount++;

    // int packetType = random(2) == 0 ? MSG_TYPE_GPS : MSG_TYPE_TIME;
    const int typeList[] = { MSG_TYPE_BATTERY, MSG_TYPE_BATTERY, MSG_TYPE_BATTERY };
    int packetType = typeList[packetCount % 3];

    buildAndSendPacket(packetType);

    openLedByNum(10, 50);
    displayLines[3] = "Sending...";
    displayLines[0] = "id:  " + deviceName + "  " + packetCount;
    displayLines[1] = getCurrentTime();
    showDisplayBy4Area(displayLines[0], displayLines[1], displayLines[2], displayLines[3]);

    // 计算下一次发送时间
    nextSendTime = calculateNextSendTime(intervalSec);
    return;
  }

  // ====== 阶段2：接收窗口（周期最后RX_WINDOW_SECONDS秒开启接收） ======
  if (didSend && canRx && !inRxMode) {
    // 基于当前时间计算周期边界，RX窗口对齐到周期最后5秒
    String timeNow = getCurrentTime();
    int h, m, s;
    sscanf(timeNow.c_str(), "%*d/%*d/%*d %d:%d:%d", &h, &m, &s);
    unsigned long timeOfDaySec = h * 3600UL + m * 60UL + s;
    unsigned long nextCycleBoundaryMs = currentMs + (intervalSec - timeOfDaySec % intervalSec) * 1000UL;
    unsigned long rxStartMs = nextCycleBoundaryMs - rxWindowMs;
    if (currentMs >= rxStartMs && currentMs < nextCycleBoundaryMs) {
      startRxWindow();
    }
  }

  // ====== 阶段3：非发送、非接收时间，休眠等待 ======
  if (didSend && !inRxMode) {
    unsigned long remaining = (nextSendTime - currentMs) / 1000;
    displayLines[3] = "Sleep " + String(remaining) + "s";
  }

  // 处理LoRa中断
  Radio.IrqProcess();

  // 更新OLED显示
  displayLines[0] = "id:  " + deviceName + "  " + packetCount;
  displayLines[1] = getCurrentTime();
  showDisplayBy4Area(displayLines[0], displayLines[1], displayLines[2], displayLines[3]);
}

// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  Radio.Sleep();
  Serial.println("✅ 发送完成");
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {
  Radio.Sleep();
  Serial.println("❌ 发送超时");
}

// ==================== LoRa接收完成回调 ====================
void onRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  Radio.Sleep();
  inRxMode = false;

  int copyLen = (size < BUFFER_SIZE) ? size : BUFFER_SIZE;
  memcpy(rxBuffer, payload, copyLen);
  rxBuffer[copyLen] = '\0';

  Serial.printf("📨 收到LoRa数据 [%d字节] RSSI:%d SNR:%d\n", size, rssi, snr);
  Serial.printf("   内容: %s\n", rxBuffer);

  // 解析消息类型，处理对时消息
  String rxStr = String(rxBuffer);
  int typeEnd = rxStr.indexOf('|');
  if (typeEnd > 0) {
    int msgType = rxStr.substring(0, typeEnd).toInt();
    if (msgType == MSG_TYPE_TIME) {
      // 提取时间字段（第二个'|'之后的内容）
      int secondPipe = rxStr.indexOf('|', typeEnd + 1);
      if (secondPipe > 0) {
        String timeStr = rxStr.substring(secondPipe + 1);
        setTimeFromLora(timeStr);
      }
    }
  }
  displayLines[3] = "RX:" + String(copyLen) + "B";
}

// ==================== LoRa接收超时回调 ====================
void onRxTimeout(void) {
  Radio.Sleep();
  inRxMode = false;
  Serial.println("⏹ RX超时，结束接收... " + getCurrentTime());
}

// ==================== 进入接收窗口 ====================
void startRxWindow() {
  inRxMode = true;
  rxStartTime = millis();
  Serial.println("📡 进入接收窗口... " + getCurrentTime());
  displayLines[3] = "RX Listening";
  Radio.Rx(0);  // 连续接收模式
}