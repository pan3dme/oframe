#include <WiFi.h>
#include <ArduinoJson.h>
#include <AliyunIoTSDK.h>

// ================= 1. 硬件与配置 =================
WiFiClient espClient;
AliyunIoTSDK iot;

// --- Wi-Fi 配置 ---
#define WIFI_SSID "yangchang"
#define WIFI_PASSWD "13787501167"

// --- 阿里云 IoT 三元组 (请确保这些信息在控制台存在) ---
#define PRODUCT_KEY "iq66cDleQPs"
// 注意：DEVICE_NAME 必须是你在控制台里创建的那个真实设备名
#define DEVICE_NAME "wifi_rola_v4_1001"
#define DEVICE_SECRET "7945f515736e7e0ae98289db07525d95"
#define REGION_ID "cn-shanghai"

// --- 外部硬件驱动 ---
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String a, String b, String c, String d);

// ================= 2. 全局变量 =================
String g_displayBuf[4] = { "", "", "", "" };

// --- 模拟设备列表 (用于业务数据上报) ---
#define DEVICE_NUM 4
String g_simDevices[DEVICE_NUM] = { "v3-1", "v3-2", "v3-3", "v3-4" };

// --- GPS 模拟数据 ---
struct GpsData {
  float lat;
  float lon;
};
GpsData g_gpsPoints[DEVICE_NUM];
bool g_gpsInitFlag = false;

// --- 认证相关 ---
String g_localDeviceName = "Unknown";  // 用于存储认证后的本地名 (如 v4-1)

// ================= 3. 核心函数 =================

void initSerial() {
  Serial.begin(115200);
  randomSeed(micros());
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  Serial.print("[Wi-Fi] 连接中");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[Wi-Fi] 连接成功! IP: " + WiFi.localIP().toString());
}

void syncNTPTime() {
  // 设置时区为中国标准时间 (UTC+8)
  setenv("TZ", "CST-8", 1);
  configTime(0, 0, "ntp.aliyun.com", "ntp.ntsc.ac.cn");

  Serial.print("[时间] 同步中");
  while (time(nullptr) < 1000000000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[时间] 同步完成!");
}

// --- 保持你原本的 makeDivceName 逻辑，仅做微调 ---
void makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  // 这里填入你原本的白名单数组
  uint64_t allowedDevices[] = {
    0x248B9C697090, 0x6809A21B5BF8, 0x8442AAAC85D8,
    0x301BA21B5BF8, 0x0C46AAAC85D8, 0x9875555
  };

  int index = -1;
  int arraySize = sizeof(allowedDevices) / sizeof(allowedDevices[0]);
  for (int i = 0; i < arraySize; i++) {
    if (currentId == allowedDevices[i]) {
      index = i;
      break;
    }
  }

  if (index != -1) {
    g_localDeviceName = "v4-" + String(index);  // ✅ 正确的变量名
    Serial.println("设备认证成功，设备名为: " + g_localDeviceName);
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
    // 即使不在白名单，也给个默认名防止后续报错
    g_localDeviceName = "v4-Unknown";
  }
}

// --- 优化后的 GPS 生成逻辑 ---
String generateGpsData(int index) {
  // 1. 初始化基准点 (仅第一次)
  if (!g_gpsInitFlag) {
    float baseLat = 26.529950;  // 湖南怀化基准点
    float baseLon = 109.390224;

    for (int i = 0; i < DEVICE_NUM; i++) {
      // 随机散布在基准点附近 ±0.004 范围内
      g_gpsPoints[i].lat = baseLat + (random(-4000, 4000) / 1000000.0);
      g_gpsPoints[i].lon = baseLon + (random(-4000, 4000) / 1000000.0);
    }
    g_gpsInitFlag = true;
  }

  // 2. 随机漫步 (每次增加微小偏移)
  g_gpsPoints[index].lat += (random(-500, 500) / 1000000.0);
  g_gpsPoints[index].lon += (random(-500, 500) / 1000000.0);

  // 3. 格式化输出
  char buffer[32];
  sprintf(buffer, "%.6f,%.6f", g_gpsPoints[index].lat, g_gpsPoints[index].lon);
  return String(buffer);
}

// ================= 4. 回调与主程序 =================

void powerCallback(JsonVariant p) {
  Serial.println("powerCallback");
  int isOpen = p["isOpen"];
  // 处理开关逻辑
}

void setup() {
  initSerial();

  // 1. 认证本地设备名 (不影响MQTT连接)
  makeDivceName();

  // 2. 连接网络
  connectWiFi();
  syncNTPTime();

  // 3. 初始化 MQTT 连接 (关键修复点：使用固定的 DEVICE_NAME)
  AliyunIoTSDK::begin(espClient, PRODUCT_KEY, DEVICE_NAME, DEVICE_SECRET, REGION_ID);
  AliyunIoTSDK::bindData("PowerSwitch", powerCallback);

  Serial.println("[系统] 启动完成");
}

void loop() {
  AliyunIoTSDK::loop();

  // --- LED 指示 ---
  openLedByNum(1, 500);

  // --- 每 5 秒上报一次数据 ---
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 5000) {
    lastSend = millis();
    openLedByNum(5, 50);

    int randomIdx = random(0, DEVICE_NUM);

    // 生成当前设备的 GPS 数据
    String gpsStr = generateGpsData(randomIdx);

    // 组装业务数据字符串
    char sendBuffer[128];
    sprintf(sendBuffer, "1|%s|%s|%s",
            g_simDevices[randomIdx].c_str(),  // 模拟的设备名
            gpsStr.c_str(),                   // GPS 坐标
            g_localDeviceName.c_str());       // 本地认证名 (v4-x)

    Serial.println("[上报] " + String(sendBuffer));
    AliyunIoTSDK::send("lorainfo", sendBuffer);

    // --- 更新屏幕显示 ---
    g_displayBuf[0] = g_simDevices[randomIdx];
    g_displayBuf[1] = gpsStr;
    g_displayBuf[2] = "";  // 可选：显示状态
    g_displayBuf[3] = g_localDeviceName;
  }

  showDisplayBy4Area(g_displayBuf[0], g_displayBuf[1], g_displayBuf[2], g_displayBuf[3]);
}