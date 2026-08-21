
#include "pan3dme.h"

#if defined(WIFI_LORA_32_V4)
SSD1306Wire
    factory_display_my(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64,
                       RST_OLED); // addr , freq , i2c group , resolution , rst
#endif

bool initFinish = false;

TinyGPSPlus gps;    // GPS对象
struct tm timeinfo; // 系统时间结构体
// 统一时间存储（GPS/LoRa/WiFi共用）
time_t syncedEpoch = 0;         // 同步的时间戳(UTC)
unsigned long syncedMillis = 0; // 同步时的本地毫秒计数

bool isGpsOn = false;

unsigned long rolaHz = 915000000; // 同步时的本地毫秒计数

// AT+CDKEY=CF673628FFEB926BD918FBA16375615D
//  设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
    0x6809A21B5BF8, // 0
    0x9875555,      // 2
    0xC0CBBE1B5BF8, // v4    2
    0x9875555,
    0x545C82697090, // v4    4
    0x9875555,
    0x1CA684697090, // v4    6
    0x9875555,
    0xF478B549FD8C, // v4    8
    0x9875555,
    0x00E7A7B2F180, // v4    10
    0x9875555,
    0xB01796A65688, // v4    12
    0x9875555,
    0x78A6B749FD8C, // v4    14
    0x9875555,
    0x10ADB749FD8C, // v4    16
    0x9875555,
    0x301BA21B5BF8, // v4    18
    0x9875555,
    0xD4A284697090, // v4    20
    0x9875555,
    0x3CB7A21B5BF8, // v4    22
    0x248B9C697090, // v4-433     23
    0xE436A21B5BF8, // v4    24  dtu
    0xF89A3604A7AC, // V3     35   三角 433
    0x20A161F61B44, // v4 NOLED   26
    0x28003A04A7AC, // v3   中继dtu   27
    0x20A261F61B44, //  v4 NOLED   28
    0x9875555       // 等待添加
};
const int DEVICE_COUNT = sizeof(allowedDevices) / sizeof(allowedDevices[0]);

void openLedByNum(int count, int delayMs) {
  pinMode(LED, OUTPUT);
  for (int i = 0; i < count; i++) {
    digitalWrite(LED, HIGH);
    delay(delayMs);
    digitalWrite(LED, LOW);
    delay(delayMs);
  }
}

// OLED统一初始化函数（给main.ino调用）
void initOLED() {
  if (!initFinish) {
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
    factory_display_my.setFont(
        ArialMT_Plain_16); // 你要的 10号 16
                           // factory_display_my.flipScreenVertically();
#endif
  }
}
void hideOLED() {
  initFinish = false;
  digitalWrite(Vext, HIGH);
  digitalWrite(RST_OLED, LOW);
}
void showOLED() {
  initFinish = false;
  initOLED();
}

void initPanGPS() {

  pinMode(VGNSS_CTRL, OUTPUT);
  pinMode(GPS_ANT_EN, OUTPUT);
  setGpsEnable(true);
}
bool getGpsStatus() { return isGpsOn; }
void setGpsEnable(bool value) {
  if (value == isGpsOn)
    return; // 【优化】如果状态没变，直接返回，避免重复操作

  if (value) {

    digitalWrite(VGNSS_CTRL, LOW);
    digitalWrite(GPS_ANT_EN, HIGH);
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    DEBUG_PRINTLN("GPS 已开启");

  } else {
    Serial2.end();
    digitalWrite(VGNSS_CTRL, HIGH);
    digitalWrite(GPS_ANT_EN, LOW);
    DEBUG_PRINTLN("GPS 已关闭");
  }

  isGpsOn = value; // 更新状态记录
}

long long setCSTTime(int year, int mon, int day, int h, int m, int s, int ms) {
  struct timeval old_tv, new_tv;
  gettimeofday(&old_tv, NULL); // 获取当前系统时间（旧）

  struct tm t = {0};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
  t.tm_isdst = 0;

  time_t utc_seconds = mktime(&t);
  new_tv.tv_sec = utc_seconds;
  new_tv.tv_usec = ms * 1000;

  // 计算偏差（微秒）
  long long diff = (new_tv.tv_sec - old_tv.tv_sec) * 1000000LL +
                   (new_tv.tv_usec - old_tv.tv_usec);

  settimeofday(&new_tv, NULL);

  return diff; // 微秒
}

void gpsEncode() {

  if (!isGpsOn) {
    return;
  }
  if (Serial2.available() > 0) {
    while (Serial2.available()) {
      gps.encode(Serial2.read());
    }
  }
  if (gps.location.isValid()) {
    int satCount = gps.satellites.value();
    //    Serial.print("当前卫星数: ");
    //    Serial.println(satCount);
    //    Serial.println(getGpsInfoStr());
  }
  // GPS时间有效时，每SEND_INTERVAL_MS周期检查一次是否需要更新
  if (!isBoardDateTimeOK()) {
    upDataGpsTimeToCs();
  }
}
void upDataGpsTimeToCs() {
  if (isBoardDateTimeOK()) {
    return;
  }
  // 新增防护，GPS时间无效直接退出
  if (!gps.date.isValid() || !gps.time.isValid()) {
    return;
  }
  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  // 2. 填入 struct tm（UTC）
  struct tm utcTm = {0};
  utcTm.tm_year = year - 1900;
  utcTm.tm_mon = month - 1;
  utcTm.tm_mday = day;
  utcTm.tm_hour = hour;
  utcTm.tm_min = minute;
  utcTm.tm_sec = second;

  // 3. 利用 mktime 将 UTC 时间转为 time_t（注意：mktime
  // 默认按本地时区，所以这里需要先设时区为 UTC）
  //    ESP32 可以使用 timegm，这里用可移植的方法：
  //    先设环境变量 TZ=UTC，然后 mktime，但可能影响全局。
  //    更简便：手动加 8 小时到 tm_hour，然后调用 mktime 自动修正。
  //    方法：直接给 tm_hour + 8，然后 mktime 会修正所有字段。
  utcTm.tm_hour += 8; // 变成北京时间的小时数（可能 >=24）

  // 调用 mktime 自动修正日期（注意：mktime 会修改 tm 结构体，并返回 time_t）
  time_t beijingTime = mktime(&utcTm); // 修正后，utcTm 就是正确的北京时间

  // 4. 此时 utcTm 已被修正，取出年月日时分秒
  int bj_year = utcTm.tm_year + 1900;
  int bj_month = utcTm.tm_mon + 1;
  int bj_day = utcTm.tm_mday;
  int bj_hour = utcTm.tm_hour;
  int bj_minute = utcTm.tm_min;
  int bj_second = utcTm.tm_sec;

  // 5. 设置系统时间（毫秒为 0，因为没有毫秒数据）
  long long diff =
      setCSTTime(bj_year, bj_month, bj_day, bj_hour, bj_minute, bj_second, 0);
  DEBUG_PRINT("✅GPS成功设置一次时间");
  if (diff >= 0) {
    DEBUG_PRINT("，时间调快了 ");
  } else {
    DEBUG_PRINT("，时间调慢了 ");
    diff = -diff;
  }
  long long diff_sec = diff / 1000000;
  long long minutes = diff_sec / 60;
  long long seconds = diff_sec % 60;
  long long millis = (diff % 1000000) / 1000;

  DEBUG_PRINT(minutes);
  DEBUG_PRINT("分");
  DEBUG_PRINT(seconds);
  DEBUG_PRINT("秒");
  DEBUG_PRINT(millis);
  DEBUG_PRINTLN("毫秒");
}

String getGpsInfoStr() {
  // int hour = gps.time.hour();
  // int minute = gps.time.minute();
  // int second = gps.time.second();
  if (gps.location.isValid() && gps.time.isValid() &&
      gps.satellites.value() > 0) {
    return String(gps.location.lat(), 5) + "," + String(gps.location.lng(), 5);
  } else {
    return "0.00000,0.00000";
  }
}

bool isReliableGPS() {
  // 1. 基本有效
  if (!gps.location.isValid())
    return false;
  // 2. 卫星数 >= 4
  if (gps.satellites.value() < 6)
    return false;
  // 3. HDOP 小于 3.0（可根据需求调整）
  
  // 4. 数据不陈旧（age < 2 秒）
  if (gps.location.age() > 2000)
    return false;
  // 5. 可选：排除 0,0（若认为无效）
  if (gps.location.lat() <= 1 && gps.location.lng() <= 1)
    return false;
  DEBUG_PRINTLN("1 ");
  // 6. ★ 新增：受时成功判断（时间必须有效）
  if (!gps.time.isValid())
    return false; // 时间有效
  DEBUG_PRINTLN("2 ");
  // 可选：也检查日期有效性
  if (!gps.date.isValid())
    return false;
  DEBUG_PRINTLN("3 ");
  // 可选：检查时间数据是否也新鲜（通常与位置同步）
  if (gps.time.age() > 2000)
    return false;
  DEBUG_PRINTLN("4 ");

  return true;
}

// 设备ID认证 (根据MAC地址生成设备名)
int getDevicesIdx() {
  uint64_t currentId = ESP.getEfuseMac();
  int index = -1;
  for (size_t i = 0; i < DEVICE_COUNT; ++i) {
    if (currentId == allowedDevices[i]) {
      index = static_cast<int>(i);
      break;
    }
  }
  return index;
}

// 获取设备总数
int getTotalDevices() { return DEVICE_COUNT; }
String makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  DEBUG_PRINTF("当前设备编号: %012llX\n", currentId);
  int index = getDevicesIdx();
  if (index != -1) {
    String syname = "vx-x";
#if defined(WIFI_LORA_32_V3)
    syname = "v3-" + String(index);
#endif
#if defined(WIFI_LORA_32_V4)
    syname = "v4-" + String(index);
#endif
    DEBUG_PRINTLN("设备认证成功，设备名为: " + syname);
    return syname;
  } else {
    DEBUG_PRINTLN("错误：该设备编号不在白名单中！");
#if defined(WIFI_LORA_32_V3)
    return "v3-x";
#endif
    return "v4-x";
  }
}
BLECallbacks initBLEFun(String deviceName, BLEServerCallbacks *serverCallbacks,
                        BLECharacteristicCallbacks *charCallbacks) {

  BLECallbacks cbs;

  BLEDevice::init("牛羊GPS" + deviceName + "-" + (rolaHz / 1000000));
  cbs.pServer = BLEDevice::createServer();
  cbs.pServer->setCallbacks(serverCallbacks);

  BLEService *pService = cbs.pServer->createService(SERVICE_UUID);
  cbs.pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE |
                               BLECharacteristic::PROPERTY_NOTIFY);
  cbs.pCharacteristic->addDescriptor(new BLE2902());
  cbs.pCharacteristic->setCallbacks(charCallbacks);
  pService->start();
  BLEDevice::startAdvertising();
  DEBUG_PRINTLN("✅ 初始化蓝牙完成");

  return cbs;
}

// 4 分区显示函数
void showDisplayBy4Area(String a, String b, String c, String d) {

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

int readBatteryEndStr() {
  analogReadResolution(12);
  pinMode(VBAT_CTRL_PIN, OUTPUT);
  bool isV4 = false;
#if defined(WIFI_LORA_32_V4)
  isV4 = true;
#endif
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

  float mvAvg = (float)mvSum / samples;

  digitalWrite(VBAT_CTRL_PIN, isV4 ? LOW : HIGH);
  delay(10);
  pinMode(VBAT_CTRL_PIN, INPUT_PULLDOWN);

  // 这里填你自己算出来的数值
 
  const float divFactor = 5.20;
  float batteryVoltage = mvAvg * divFactor / 1000.0;

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 99);
  soc = constrain(soc, 0, 99);
  float socRatio = soc / 100.0;

  String outStr = String(socRatio, 1) + "|" + String(batteryVoltage, 1);
  Serial.print("电量信息：");
  Serial.print(outStr);
  Serial.print(" ");
  Serial.print(soc);
  Serial.println("%");

  return soc;
}


bool isBoardDateTimeOK() {
  time_t now;
  struct tm t;
  time(&now);
  gmtime_r(&now, &t); // UTC
  mktime(&t);     // 处理时间进位
  int year = t.tm_year + 1900;
  return (year > 2025) && (year < 2030);
}
// 获取可用的时间字符串 (优先同步时间，最后默认运行时间)
String getCurrentTime(bool includeMillis) {
  struct timeval tv;
  gettimeofday(&tv, nullptr); // 获取秒 + 微秒
  time_t now = tv.tv_sec;
  struct tm t;
  localtime_r(&now, &t); // 自动使用系统时区

  char buf[64];
  if (includeMillis) {
    // 包含毫秒（格式：YYYY/MM/DD HH:MM:SS.mmm）
    snprintf(buf, sizeof(buf), "%4d/%d/%d %02d:%02d:%02d.%03d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
             t.tm_sec,
             (int)(tv.tv_usec / 1000)); // 微秒 → 毫秒
  } else {
    // 不包含毫秒（格式：YYYY/MM/DD HH:MM:SS）
    snprintf(buf, sizeof(buf), "%4d/%d/%d %02d:%02d:%02d", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
  return String(buf);
}

// 获取当前时间戳（毫秒级）
long long getCurrentTimestampMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

long long getCurrentTimestampSec() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (long long)tv.tv_sec; // tv_sec 本身就是秒数
}
uint32_t getTodaySecond() {
  time_t now = time(nullptr);
  struct tm local_tm;
  localtime_r(&now, &local_tm);

  // 当天已经过去的秒 = 时*3600 + 分*60 + 秒
  uint32_t sec =
      local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;
  return sec;
}
// 通过时间戳（毫秒）设置系统时间
void setTimeFromTimestamp(long long epochMs) {
  struct timeval new_tv;
  new_tv.tv_sec = (time_t)(epochMs / 1000LL);
  new_tv.tv_usec = (suseconds_t)((epochMs % 1000LL) * 1000);
  settimeofday(&new_tv, NULL);
}
void setTimeFromTimestampSec(long long epochSec) {
  struct timeval tv;
  tv.tv_sec = (time_t)epochSec; // 秒部分
  tv.tv_usec = 0;               // 微秒清零（无毫秒精度）
  settimeofday(&tv, NULL);
}
long long mathTimeDiffmsFromSec(long long epochSec) {
  long long nowSec = getCurrentTimestampSec(); // 获取当前秒时间戳
  return nowSec - epochSec;                    // 返回差值（ms）
}

void printTimestampSec(long long epochSec, const char *label) {
  time_t sec = (time_t)epochSec; // 直接使用秒
  struct tm t;
  localtime_r(&sec, &t);
  DEBUG_PRINTF("%s%4d/%d/%d %02d:%02d:%02d\n", label, t.tm_year + 1900,
               t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}
// 将秒差值打印为 N小时N分N秒
void printDurationSec(long long diffSec, const char *label) {
  bool negative = diffSec < 0;
  if (negative) {
    diffSec = -diffSec;
  }
  long long hours = diffSec / 3600LL;
  long long minutes = (diffSec % 3600LL) / 60LL;
  long long seconds = diffSec % 60LL;
  // 不再输出毫秒
  DEBUG_PRINTF("%s%s%lld小时%lld分%lld秒\n", label, negative ? "-" : "", hours,
               minutes, seconds);
}

// 从LoRa对时信息设置时间2026/07/14 23:23:10.513
void setTimeFromLora(String timeStr) {
  int year, month, day, hour, minute, second, millis = 0;
  // 尝试解析带毫秒
  if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d.%d", &year, &month, &day,
             &hour, &minute, &second, &millis) == 7) {
    // 解析成功，millis已赋值
  } else if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d", &year, &month, &day,
                    &hour, &minute, &second) == 6) {
    millis = 0; // 无毫秒
  } else {
    DEBUG_PRINT("❌ LoRa对时解析失败: ");
    DEBUG_PRINTLN(timeStr);
    return;
  }
  // 优化偏移 这个代码还要验证LORA互相传输延时暂定200毫秒
  //  if(millis<800){
  //    millis+=200;
  //  }else{
  //    if(second<59){
  //      second+=1;
  //      millis-=800;
  //    }else{
  //      Serial.println("不做LORA对时偏移");
  //    }
  //  }

  setCSTTime(year, month, day, hour, minute, second, millis);
}

void initPanRadio(RadioEvents_t *radioEvents, int txPower, unsigned long hzFreq,
                  int swNum) {
  // void initPanRadio(RadioEvents_t *radioEvents, int txPower) {
  rolaHz = hzFreq;
  Radio.Init(radioEvents);
  Radio.SetChannel(rolaHz);

  Radio.SetRxConfig(MODEM_LORA, LORA_BW, LORA_SF, LORA_CR, 0, PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, false);

  // 发送参数配置
  Radio.SetTxConfig(MODEM_LORA, txPower, 0, LORA_BW, LORA_SF, LORA_CR,
                    PREAMBLE_LENGTH, false, true, 0, 0, false, 1000);

  DEBUG_PRINT("✅ 当前lora频段");
  DEBUG_PRINT(rolaHz);
  DEBUG_PRINT(" 发射功率");
  DEBUG_PRINT(txPower);
  DEBUG_PRINT(" SF");
  DEBUG_PRINTLN(swNum);
  DEBUG_PRINTLN("✅ LoRa 初始化完成");
}

bool isTimeInRange(long long timestampSec, const char *timeRangeStr) {
  int startH = 0, startM = 0, endH = 0, endM = 0;
  if (sscanf(timeRangeStr, "%d:%d-%d:%d", &startH, &startM, &endH, &endM) !=
      4) {
    DEBUG_PRINT("❌ isTimeInRange 解析失败: ");
    DEBUG_PRINTLN(timeRangeStr);
    return true; // 解析失败默认在范围内
  }

  // 将时间戳转为本地时间的时和分（现在是秒，无需除以1000）
  time_t sec = (time_t)timestampSec; // 直接转换
  struct tm t;
  localtime_r(&sec, &t);
  int nowMinutes = t.tm_hour * 60 + t.tm_min;

  int startMinutes = startH * 60 + startM;
  int endMinutes = endH * 60 + endM;

  // 处理跨午夜
  if (startMinutes <= endMinutes) {
    return nowMinutes >= startMinutes && nowMinutes <= endMinutes;
  } else {
    return nowMinutes >= startMinutes || nowMinutes <= endMinutes;
  }
}

int timeWindowToIndex(uint8_t start, uint8_t end) {
  uint8_t realEnd;
  if (end >= 24) {
    realEnd = 23;
  } else {
    realEnd = end;
  }

  if (start >= realEnd)
    return -1;
  // 这里删掉 >=3 的判断，只要求 start < realEnd，最小1小时

  int preCount = 0;
  for (uint8_t s = 0; s < start; s++) {
    // s+1 开始，最小间隔1小时
    int valid = 23 - (s + 1) + 1;
    if (valid > 0)
      preCount += valid;
  }
  int curOffset = realEnd - (start + 1);
  return preCount + curOffset;
}

bool indexToTimeWindow(int idx, uint8_t &outStart, uint8_t &outEnd) {
  if (idx < 0)
    return false;
  int sum = 0;
  for (uint8_t s = 0; s <= 23; s++) {
    // 最小间隔1小时
    int valid = 23 - (s + 1) + 1;
    if (valid <= 0)
      continue;
    if (idx < sum + valid) {
      outStart = s;
      int off = idx - sum;
      outEnd = s + 1 + off;
      return true;
    }
    sum += valid;
  }
  return false;
}

const char TIME_DICT[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

String indexToTwoChar(int idx) {
  if (idx < 0 || idx > 275)
    return "XX"; // 最大索引275
  int high = idx / 62;
  int low = idx % 62;
  String res;
  res += TIME_DICT[high];
  res += TIME_DICT[low];
  return res;
}

int twoCharToIndex(String str) {
  if (str.length() != 2)
    return -1;
  char c0 = str[0];
  char c1 = str[1];
  const char *p0 = strchr(TIME_DICT, c0);
  const char *p1 = strchr(TIME_DICT, c1);
  if (p0 == nullptr || p1 == nullptr)
    return -1;
  int h = p0 - TIME_DICT;
  int l = p1 - TIME_DICT;
  return h * 62 + l;
}

/**
 * @brief 矩形框内输出经纬度差分(系数1e5)，框外原样返回原始字符串
 * @param inBuf 输入 "lat,lon" 带小数
 * @param outBuf 输出32字节缓冲区
 * @param baseLat 基准纬度(小数)
 * @param baseLon 基准经度(小数)
 * @param latHalf 纬度半宽(度)
 * @param lonHalf 经度半宽(度)
 */
void filterGpsByRect(const char *inBuf, char *outBuf, double baseLat,
                     double baseLon, double latHalf, double lonHalf) {
  const char *comma = strchr(inBuf, ',');
  if (comma == nullptr) {
    strncpy(outBuf, inBuf, 31);
    outBuf[31] = '\0';
    return;
  }

  char latStr[16];
  strncpy(latStr, inBuf, comma - inBuf);
  latStr[comma - inBuf] = '\0';

  double srcLat = atof(latStr);
  double srcLon = atof(comma + 1);

  double latMin = baseLat - latHalf;
  double latMax = baseLat + latHalf;
  double lonMin = baseLon - lonHalf;
  double lonMax = baseLon + lonHalf;

  bool inRect = (srcLat >= latMin && srcLat <= latMax) &&
                (srcLon >= lonMin && srcLon <= lonMax);

  if (!inRect) {
    strncpy(outBuf, inBuf, 31);
    outBuf[31] = '\0';
  } else {
    double dLat = srcLat - baseLat;
    double dLon = srcLon - baseLon;

    // 系数 100000，示例：‑0.00017 *100000 = -17
    int32_t deltaLat = (int32_t)(dLat * 100000LL);
    int32_t deltaLon = (int32_t)(dLon * 100000LL);

    snprintf(outBuf, 32, "%ld,%ld", (long)deltaLat, (long)deltaLon);
  }
}

/**
 * @brief 把差分字符串还原回原始带5位小数GPS字符串
 * @param diffBuf 输入：两种格式
 *        ①框内差分："-17,-22" 整数差分
 *        ②框外原始："26.52941,109.39065"原样直接复制
 * @param outBuf 输出32字节，还原 "lat.xxxxx,lon.xxxxx"
 * @param baseLat 基准纬度小数
 * @param baseLon 基准经度小数
 */
void restoreGpsFromDiff(const char *diffBuf, char *outBuf, double baseLat,
                        double baseLon) {
  const char *comma = strchr(diffBuf, ',');
  if (comma == nullptr) {
    strncpy(outBuf, diffBuf, 31);
    outBuf[31] = '\0';
    return;
  }

  // 判断是差分(没有小数点)，还是原始带小数数据
  bool isDiff = (strchr(diffBuf, '.') == nullptr);

  if (!isDiff) {
    // 带小数点，属于框外原始数据，直接拷贝
    strncpy(outBuf, diffBuf, 31);
    outBuf[31] = '\0';
    return;
  }

  // 是差分数据，解析deltaLat deltaLon
  char latStr[16];
  strncpy(latStr, diffBuf, comma - diffBuf);
  latStr[comma - diffBuf] = '\0';

  int32_t deltaLat = atoi(latStr);
  int32_t deltaLon = atoi(comma + 1);

  // 还原公式 base + delta /100000.0
  double realLat = baseLat + deltaLat / 100000.0;
  double realLon = baseLon + deltaLon / 100000.0;

  snprintf(outBuf, 32, "%.5f,%.5f", realLat, realLon);
}
bool splitPipeSegment(const char *in, char *out, int idx) {
  int cur = 0;
  const char *start = in;
  const char *p = in;

  while (*p != '\0') {
    if (*p == '|') {
      if (cur == idx) {
        size_t len = p - start;
        strncpy(out, start, len);
        out[len] = '\0';
        return true;
      }
      cur++;
      start = p + 1;
    }
    p++;
  }
  // 处理最后一段
  if (cur == idx) {

    strncpy(out, start, sizeof(out) - 1);
    return true;
  }
  out[0] = '\0';
  return false;
}
bool replacePipeSegment(const char *src, char *dest, int idx,
                        const char *newVal, size_t destSize) {
  if (destSize == 0)
    return false;
  dest[0] = '\0';

  int curIdx = 0;
  const char *p = src;
  const char *segStart = src;

  while (*p != '\0') {
    if (*p == '|') {
      if (curIdx == idx) {
        strncat(dest, newVal, destSize - 1 - strlen(dest));
      } else {
        size_t segLen = p - segStart;
        char tmp[64];
        strncpy(tmp, segStart, segLen);
        tmp[segLen] = '\0';
        strncat(dest, tmp, destSize - 1 - strlen(dest));
      }
      strncat(dest, "|", destSize - 1 - strlen(dest));
      curIdx++;
      segStart = p + 1;
    }
    p++;
  }

  // 处理最后一段
  if (curIdx == idx) {
    strncat(dest, newVal, destSize - 1 - strlen(dest));
  } else {
    strncat(dest, segStart, destSize - 1 - strlen(dest));
  }
  return true;
}
// segBuf：上报当日秒偏移，outBuf输出unix时间戳字符串，outBufLen缓冲区大小
// 返回true成功，false无效
bool buildFullTimestampStr(const char *segBuf, char *outBuf, size_t outBufLen) {
  uint32_t daySec = atoi(segBuf);
  if (daySec > 86399) {
    return false;
  }

  time_t now = time(nullptr);
  struct tm local_tm;
  localtime_r(&now, &local_tm);

  uint32_t h = daySec / 3600;
  uint32_t m = (daySec % 3600) / 60;
  uint32_t s = daySec % 60;

  local_tm.tm_hour = h;
  local_tm.tm_min = m;
  local_tm.tm_sec = s;

  time_t ts = mktime(&local_tm);
  // 转成数字字符串写入缓冲区
  snprintf(outBuf, outBufLen, "%llu", (uint64_t)ts);
  return true;
}