
#include "pan3dme.h"



#if defined(WIFI_LORA_32_V4)
SSD1306Wire factory_display_my(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst
#endif

bool initFinish = false;

 
TinyGPSPlus gps;                            // GPS对象
struct tm timeinfo;                         // 系统时间结构体
bool wifiTimeSynced = false;                // 是否已成功同步网络时间
time_t syncedEpoch = 0;                     // 成功同步的时间戳
unsigned long syncedMillis = 0;             // 同步时的本地毫秒计数

// 设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
    0x248B9C697090, // v4    1
    0x6809A21B5BF8, // v4    2
    0x8442AAAC85D8, // v3    3
    0x301BA21B5BF8, // v4    4
    0x0C46AAAC85D8, // v3    5
    0xB4E00404A7AC, // v3    6
    0xF89A3604A7AC, // v3    7
    0x9875555       // 等待添加
};

void openLedByNum(int count, int delayMs)
{
  pinMode(LED, OUTPUT);
  for (int i = 0; i < count; i++)
  {
    digitalWrite(LED, HIGH);
    delay(delayMs);
    digitalWrite(LED, LOW);
    delay(delayMs);
  }
}

// OLED统一初始化函数（给main.ino调用）
void initOLED()
{
  if (!initFinish)
  {
#if defined(WIFI_LORA_32_V4)
    initFinish = true;
    // 开启OLED电源
    pinMode(Vext, OUTPUT); // 36
    digitalWrite(Vext, LOW);
    pinMode(RST_OLED, OUTPUT); // 21
    digitalWrite(RST_OLED, HIGH);
    delay(1);
    digitalWrite(RST_OLED, LOW);
    delay(1);
    digitalWrite(RST_OLED, HIGH);
    delay(1);

    factory_display_my.init();
    factory_display_my.setFont(ArialMT_Plain_16); // 你要的 10号 16
    // factory_display_my.flipScreenVertically();
#endif
  }
}

void initPanGPS()
{
  pinMode(VGNSS_CTRL, OUTPUT);
  digitalWrite(VGNSS_CTRL, LOW); // 开启GPS电源
  pinMode(GPS_ANT_EN, OUTPUT);
  digitalWrite(GPS_ANT_EN, HIGH); // 开启天线供电
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS 已启动");
}
void gpsEncode()
{
  if (Serial1.available() > 0) {
    while (Serial1.available()) {
      gps.encode(Serial1.read());
    }
  }
}

// 获取GPS时间 (带自动同步逻辑)
// 获取GPS时间并转换为北京时间 (UTC+8)
String getCurrentGpsTm(TinyGPSPlus &gps)
{
  if (!gps.time.isValid())
    return "no gps time";

  // 1. 获取GPS原始时间（UTC）
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();
  int day = gps.date.day();
  int month = gps.date.month();
  int year = gps.date.year();

  // 2. 转换为北京时间 (UTC+8)
  // 简单的加法
  hour += 8;

  // 3. 处理进位（跨天）
  // 如果小时数大于等于24，需要进位到天
  if (hour >= 24)
  {
    hour -= 24;
    day += 1;

    // 注意：这里仅做了简单的+1处理，未处理跨月/跨年（对于调试显示足够）
    // 如果需要严谨的日期计算，建议使用 time_t 和 mktime，但考虑到内存限制，此处简化
  }

  // 4. 格式化输出
  char timeStr[30];
  snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d",
           year, month, day, hour, minute, second);

  return String(timeStr); // 返回 月/日 时:分:秒
}

// 设备ID认证 (根据MAC地址生成设备名)
String makeDivceName()
{
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  int index = -1;
  for (size_t i = 0; i < sizeof(allowedDevices) / sizeof(allowedDevices[0]); ++i)
  {
    if (currentId == allowedDevices[i])
    {
      index = static_cast<int>(i);
      break;
    }
  }
  if (index != -1)
  {
    String syname = "vx-x";
#if defined(WIFI_LORA_32_V3)
    syname = "v3-" + String(index);
#endif
#if defined(WIFI_LORA_32_V4)
    syname = "v4-" + String(index);
#endif
    Serial.println("设备认证成功，设备名为: " + syname);
    return syname;
  }
  else
  {
    Serial.println("错误：该设备编号不在白名单中！");
#if defined(WIFI_LORA_32_V3)
    return "v3-x";
#endif
    return "v4-x";
  }
}
BLECallbacks initBLEFun(String deviceName, BLEServerCallbacks *serverCallbacks, BLECharacteristicCallbacks *charCallbacks)
{

  BLECallbacks cbs;

  BLEDevice::init("牛羊GPS" + deviceName);
  cbs.pServer = BLEDevice::createServer();
  cbs.pServer->setCallbacks(serverCallbacks);

  BLEService *pService = cbs.pServer->createService(SERVICE_UUID);
  cbs.pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  cbs.pCharacteristic->addDescriptor(new BLE2902());
  cbs.pCharacteristic->setCallbacks(charCallbacks);
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("✅ 初始化蓝牙完成");

  return cbs;
}

// 4 分区显示函数
void showDisplayBy4Area(String a, String b, String c, String d)
{

#if defined(WIFI_LORA_32_V4)
  initOLED();
  factory_display_my.clear();
  factory_display_my.drawString(0, 0, a);  // 行1
  factory_display_my.drawString(0, 16, b); // 行2
  factory_display_my.drawString(0, 32, c); // 行4
  factory_display_my.drawString(0, 48, d); // 行3
  factory_display_my.display();
#endif
}

// 初始化WiFi并同步网络时间
// 初始化WiFi并同步网络时间 (获取后自动断开以省电)


// 获取可用的时间字符串 (优先网络，其次GPS，最后默认)
String getCurrentTime() {
  if (wifiTimeSynced) {
    unsigned long elapsedMs = millis() - syncedMillis;
    time_t currentEpoch = syncedEpoch + elapsedMs / 1000;
    struct tm *tmNow = localtime(&currentEpoch);
    if (tmNow != NULL) {
      char timeStr[30];
      snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d",
               tmNow->tm_year + 1900,
               tmNow->tm_mon + 1,
               tmNow->tm_mday,
               tmNow->tm_hour,
               tmNow->tm_min,
               tmNow->tm_sec);
      return String(timeStr);
    }else{
      return"2000/0/0 0:0:0";
    }
  }

  // 如果没有网络时间，则尝试使用 GPS 时间
  // if (gps.time.isValid() && gps.date.isValid()) {
   
  // }

  return "0000/00/00 00:00:00";
}
bool initLibWifi(  )
{
    const char *ssid = "yangchang";
  const char *password = "13787501167";

  Serial.print("正在连接 WiFi");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ WiFi 连接失败，跳过网络时间同步");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.println("\n✅ WiFi 连接成功，开始同步网络时间");
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 50) {
    delay(100);
    retry++;
  }

  if (retry >= 50) {
    Serial.println("❌ 网络时间获取失败");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // 成功获取到网络当前时间
  syncedEpoch = mktime(&timeinfo);
  syncedMillis = millis();
  wifiTimeSynced = true;
  Serial.println("✅ 网络时间同步成功");
  Serial.print("同步时间：");
  Serial.print(timeinfo.tm_year + 1900);
  Serial.print("/");
  Serial.print(timeinfo.tm_mon + 1);
  Serial.print("/");
  Serial.print(timeinfo.tm_mday);
  Serial.print(" ");
  Serial.print(timeinfo.tm_hour);
  Serial.print(":");
  Serial.print(timeinfo.tm_min);
  Serial.print(":");
  Serial.println(timeinfo.tm_sec);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("📶 WiFi 已关闭，后续时间使用本地时钟增量");
  return true;
}
