/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"

// ==================== 常量定义 ====================
const char* DEVICE_NAME_PREFIX = "v4-x";
const unsigned long RX_WINDOW_SECONDS = 5;  // 接收窗口秒数（周期最后N秒用于接收）


// ==================== 全局变量 ====================
String deviceName;               // 设备名称
String gpsCoordinates;           // GPS坐标信息
char sendData[BUFFER_SIZE];      // 发送数据缓存
RadioEvents_t radioEvents;       // LoRa事件回调
int packetCount = 0;             // 数据包计数器
unsigned long lastSendTime = 0;  // 上次发送时间戳
String displayLines[4];          // OLED显示内容

// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）
time_t bootEpoch = 946684800;    // 开机基准时间：2000/1/1 0:0:0 (UTC)
unsigned long bootMillis = 0;    // 开机时的millis()

// LoRa接收窗口状态
bool inRxMode = false;            // 当前是否处于接收模式
bool didSend = false;             // 本周期是否已发送（控制RX窗口和休眠）
char rxBuffer[BUFFER_SIZE + 1];   // 接收数据缓存

// ==================== 计算下次发送时间 ====================
unsigned long calculateNextSendTime(unsigned long intervalSeconds) {
  // 从pan3dme获取设备索引和总数
  if (deviceIndex < 0 || totalDevices == 0) {
    deviceIndex = getDevicesIdx();
    totalDevices = getTotalDevices();
    Serial.printf("设备索引: %d, 总设备数: %d\n", deviceIndex, totalDevices);
  }

  if (deviceIndex < 0 || totalDevices <= 0) {
    Serial.println("⚠️ 设备未认证，使用默认间隔");
    return millis() + intervalSeconds * 1000;
  }

  // 获取当前推算时间（基于开机时间+系统运行时间）
  unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
  time_t currentEpoch = bootEpoch + elapsedSeconds;
  struct tm* tmNow = localtime(&currentEpoch);

  int hour = tmNow->tm_hour;
  int minute = tmNow->tm_min;
  int second = tmNow->tm_sec;

  // 基于推算时间计算时隙
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

  Serial.printf("推算时间: %02d:%02d:%02d, 设备%d, 时隙%.2f秒, 延迟%lu ms\n",
                hour, minute, second, deviceIndex, slotDuration, delayMillis);

  return millis() + delayMillis;
}

// ==================== 判断是否到达发送时间 ====================
bool isTimeToSend(unsigned long intervalSeconds) {
  unsigned long currentMillis = millis();

  // 首次调用或已过发送时间，重新计算
  if (nextSendTime == 0 || currentMillis >= nextSendTime) {
    nextSendTime = calculateNextSendTime(intervalSeconds);
    return true;  // 立即发送
  }

  return false;
}




// ==================== 读取GPS信息 ====================
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
// ==================== 系统初始化 ====================
void setup() {
  delay(1000);
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // 初始化开机基准时间（2000/1/1 0:0:0 UTC）
  bootMillis = millis();
  Serial.println("开机基准时间: 2000/1/1 0:0:0 UTC");

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
    dataStr += "|" + gpsCoordinates + "|" + String(packetCount);
  } else if (packetType == MSG_TYPE_TIME) {
    dataStr += "|" + getCurrentTime();
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

  // ====== 阶段1：到达发送时间，执行发送 ======
  if (currentMs >= nextSendTime) {
    // 如果正在接收，先退出RX模式
    if (inRxMode) {
      Radio.Sleep();
      inRxMode = false;
    }

    didSend = true;
    packetCount++;

    int packetType = random(2) == 0 ? MSG_TYPE_GPS : MSG_TYPE_TIME;
    if (packetType == MSG_TYPE_GPS) {
      updateGpsInfo();
    }
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

  // ====== 阶段2：接收窗口（已发送，距离下次发送不足RX_WINDOW_SECONDS时持续监听） ======
  if (didSend && canRx && !inRxMode) {
    unsigned long timeUntilSend = nextSendTime - currentMs;
    if (timeUntilSend <= rxWindowMs && timeUntilSend > 0) {
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
void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Radio.Sleep();
  inRxMode = false;

  int copyLen = (size < BUFFER_SIZE) ? size : BUFFER_SIZE;
  memcpy(rxBuffer, payload, copyLen);
  rxBuffer[copyLen] = '\0';

  Serial.printf("📨 收到LoRa数据 [%d字节] RSSI:%d SNR:%d\n", size, rssi, snr);
  Serial.printf("   内容: %s\n", rxBuffer);


}

// ==================== LoRa接收超时回调 ====================
void onRxTimeout(void) {
  Radio.Sleep();
  inRxMode = false;
}

// ==================== 进入接收窗口 ====================
void startRxWindow() {
  inRxMode = true;
  Serial.println("📡 进入接收窗口...");
  displayLines[3] = "RX Listening";
  Radio.Rx(0);  // 连续接收模式
}