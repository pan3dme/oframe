
#include "pan3dme.h"

#if defined(WIFI_LORA_32_V4)
SSD1306Wire factory_display_my(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst
#endif

bool initFinish = false;

TinyGPSPlus gps;                // GPS对象
struct tm timeinfo;             // 系统时间结构体
// 统一时间存储（GPS/LoRa/WiFi共用）
time_t syncedEpoch = 0;         // 同步的时间戳(UTC)
unsigned long syncedMillis = 0; // 同步时的本地毫秒计数


bool isGpsOn = false;

//AT+CDKEY=CF673628FFEB926BD918FBA16375615D
// 设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x248B9C697090, // v4    4
    0x9875555,
    0x6809A21B5BF8, // v4    6
    0x9875555,
    0x8442AAAC85D8, // v3    8
    0x9875555,
    0x301BA21B5BF8, // v4    10
    0x9875555,
    0x0C46AAAC85D8, // v3    12
    0x9875555,
    0xB4E00404A7AC, // v3    14
    0x9875555,
    0xF89A3604A7AC, // v3    15
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555,
    0x9875555       // 等待添加
};
const int DEVICE_COUNT = sizeof(allowedDevices) / sizeof(allowedDevices[0]);

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
void hideOLED(){
    digitalWrite(Vext, HIGH);
    digitalWrite(RST_OLED, LOW);
}
void showOLED(){
    initFinish= false;
    initOLED();
}

void initPanGPS()
{
    pinMode(VGNSS_CTRL, OUTPUT);
    pinMode(GPS_ANT_EN, OUTPUT);
    setGpsEnable(false);
    delay(500);
    setGpsEnable(true);
    delay(500);
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

}
bool getGpsStatus() {
    return isGpsOn;
}
void  setGpsEnable(bool  value){
    if (value == isGpsOn) return; // 【优化】如果状态没变，直接返回，避免重复操作

    if (value) {
        digitalWrite(VGNSS_CTRL, LOW);
        digitalWrite(GPS_ANT_EN, HIGH);
        Serial.println("GPS 已开启");
    } else {
        Serial2.end();
        digitalWrite(VGNSS_CTRL, HIGH);
        digitalWrite(GPS_ANT_EN, LOW);
        Serial.println("GPS 已关闭");
    }

    isGpsOn = value; // 更新状态记录
}
// 统一时间更新：仅当新时间比已存储时间更新时才替换
void updateSyncedTime(time_t newEpoch, const char* source)
{
  if (syncedEpoch > 0) {
    time_t currentEstimate = syncedEpoch + (millis() - syncedMillis) / 1000;
    if (newEpoch <= currentEstimate) {
//      Serial.printf("%s时间较旧，跳过: new=%ld <= cur=%ld\n", source, (long)newEpoch, (long)currentEstimate);
      return;
    }
  }
  syncedEpoch = newEpoch;
  syncedMillis = millis();
  Serial.printf("✅ %s对时成功, epoch=%ld\n", source, (long)syncedEpoch);
}

void gpsEncode()
{
    if(!isGpsOn){
        return;
    }
  if (Serial2.available() > 0)
  {
    while (Serial2.available())
    {
      gps.encode(Serial2.read());
    }
  }

  // GPS时间有效时，每SEND_INTERVAL_MS周期检查一次是否需要更新
  if ((syncedEpoch == 0 || millis() - syncedMillis >= SEND_INTERVAL_MS)
      && gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2025)
  {
    struct tm tmGps;
    memset(&tmGps, 0, sizeof(tmGps));
    tmGps.tm_year = gps.date.year() - 1900;
    tmGps.tm_mon = gps.date.month() - 1;
    tmGps.tm_mday = gps.date.day();
    tmGps.tm_hour = gps.time.hour(); // 存UTC时间，不手动+8
    tmGps.tm_min = gps.time.minute();
    tmGps.tm_sec = gps.time.second();
    time_t newEpoch = mktime(&tmGps);
    updateSyncedTime(newEpoch, "GPS");
  }
}
String getGpsInfoStr(){
  // int hour = gps.time.hour();
  // int minute = gps.time.minute();
  // int second = gps.time.second();
   if (gps.location.isValid() && gps.time.isValid() && gps.satellites.value() > 0) {
     return  String(gps.location.lat(), 5) + "," + String(gps.location.lng(), 5);
   } else {
     return "0.00000,0.00000";
   }
}


// 设备ID认证 (根据MAC地址生成设备名)
int getDevicesIdx(){
  uint64_t currentId = ESP.getEfuseMac();
  int index = -1;
  for (size_t i = 0; i < DEVICE_COUNT; ++i)
  {
    if (currentId == allowedDevices[i])
    {
      index = static_cast<int>(i);
      break;
    }
  }
  return index;
}

// 获取设备总数
int getTotalDevices() {
  return DEVICE_COUNT;
}
String makeDivceName()
{
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);
  int index = getDevicesIdx();
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

  BLEDevice::init("牛羊GPS" + deviceName+"-"+(LORA_FREQ/1000000));
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

// 从UTC epoch输出北京时间字符串 (UTC+8)
String epochToBeijingStr(time_t epoch)
{
  struct tm *tmUtc = gmtime(&epoch);
  if (tmUtc == NULL) return "";
  int hour = tmUtc->tm_hour + 8;
  int day = tmUtc->tm_mday;
  int month = tmUtc->tm_mon + 1;
  int year = tmUtc->tm_year + 1900;
  if (hour >= 24) {
    hour -= 24;
    // 简化进位：仅+1天（跨月/跨年交给mktime精确计算）
    time_t nextDay = epoch + 86400;
    struct tm *tmNext = gmtime(&nextDay);
    if (tmNext) { day = tmNext->tm_mday; month = tmNext->tm_mon + 1; year = tmNext->tm_year + 1900; }
  }
  char timeStr[30];
  snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d", year, month, day, hour, tmUtc->tm_min, tmUtc->tm_sec);
  return String(timeStr);
}

// 获取可用的时间字符串 (优先同步时间，最后默认运行时间)
String getCurrentTime()
{
  if (syncedEpoch > 0)
  {
    unsigned long elapsedMs = millis() - syncedMillis;
    time_t currentEpoch = syncedEpoch + elapsedMs / 1000;
    String s = epochToBeijingStr(currentEpoch);
    if (s.length() > 0) return s;
  }

  // 兜底：开机时间 + 运行时间
  time_t bootEpoch = 946684800; // 2000/1/1 00:00:00 UTC
  time_t currentEpoch = bootEpoch + millis() / 1000;
  String s = epochToBeijingStr(currentEpoch);
  if (s.length() > 0) return s;
  return "2000/1/1 08:00:00";
}
bool initLibWifi()
{
  const char *ssid = "yangchang";
  const char *password = "13787501167";

  Serial.print("正在连接 WiFi");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
  {
    delay(100);
    openLedByNum(1, 50);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\n⚠️ WiFi 连接失败，跳过网络时间同步");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.println("\n✅ WiFi 连接成功，开始同步网络时间");
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 50)
  {
    delay(100);
    retry++;
  }

  if (retry >= 50)
  {
    Serial.println("❌ 网络时间获取失败");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // 成功获取到网络当前时间
  syncedEpoch = mktime(&timeinfo);
  syncedMillis = millis();
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
  return true;
}
void disConnectWifi(){
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("📶 WiFi 已关闭，后续时间使用本地时钟增量");
}

// 从LoRa对时信息设置时间（仅当新时间比本地时间更新时才覆盖）
void setTimeFromLora(String timeStr)
{
  struct tm tmLora;
  memset(&tmLora, 0, sizeof(tmLora));

  int year, month, day, hour, minute, second;
  if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d",
             &year, &month, &day, &hour, &minute, &second) == 6)
  {
    // LoRa时间是北京时间，先转UTC再存epoch
    hour -= 8;
    if (hour < 0) {
      hour += 24;
      day -= 1;
      if (day < 1) {
        day = 28; month -= 1;
        if (month < 1) { month = 12; year -= 1; }
      }
    }
    tmLora.tm_year = year - 1900;
    tmLora.tm_mon = month - 1;
    tmLora.tm_mday = day;
    tmLora.tm_hour = hour;
    tmLora.tm_min = minute;
    tmLora.tm_sec = second;

    time_t newEpoch = mktime(&tmLora);
    updateSyncedTime(newEpoch, "LoRa");
  }
  else
  {
    Serial.print("❌ LoRa对时解析失败: ");
    Serial.println(timeStr);
  }
}

// 判断是否有有效时间
bool hasValidTime()
{
  return syncedEpoch > 0;
}

void initPanRadio(RadioEvents_t* radioEvents) {

  Radio.Init(radioEvents);
  Radio.SetChannel(LORA_FREQ);
  Serial.print("✅ 当前lora频段");
  Serial.println(LORA_FREQ);
  Radio.SetRxConfig(MODEM_LORA, LORA_BW, LORA_SF,
                    LORA_CR, 0, PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, false);

  // 发送参数配置
  Radio.SetTxConfig(MODEM_LORA, TX_POWER, 0, LORA_BW,
                    LORA_SF, LORA_CR, PREAMBLE_LENGTH, false,
                    true, 0, 0, false, 1500);

  Serial.println("✅ LoRa 初始化完成");

#if defined(WIFI_LORA_32_V4)
  // 上电初始化功放 - 开启高功率模式
//  pinMode(LORA_PA_POWER, OUTPUT);
//  digitalWrite(LORA_PA_POWER, HIGH);
//  pinMode(LORA_PA_EN, OUTPUT);
//  digitalWrite(LORA_PA_EN, HIGH);
//  pinMode(LORA_PA_TX_EN, OUTPUT);
//  digitalWrite(LORA_PA_TX_EN, HIGH);
//  Serial.println("✅ 已开启高功率模式");
#endif


}