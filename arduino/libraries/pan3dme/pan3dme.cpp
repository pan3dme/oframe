
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

// AT+CDKEY=CF673628FFEB926BD918FBA16375615D
//  设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
    0x9875555,      0x9875555,
    0xC0CBBE1B5BF8, // v4    no oled
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
    0xF89A3604A7AC, // v3    16
    0x9875555,
    0x28003A04A7AC, // v3    18
    0x9875555,
    0x40BC0604A7AC, // v3    20
    0x9875555,
    0x3CB7A21B5BF8, // v4    22
    0x9875555,
    0xE436A21B5BF8, // v4    24
    0x9875555,
    0x20A161F61B44, // v4 NOLED   26
    0x9875555,
    0x20A261F61B44,//  v4 NOLED   28
    0x9875555 // 等待添加
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
  digitalWrite(Vext, HIGH);
  digitalWrite(RST_OLED, LOW);
}
void showOLED() {
  initFinish = false;
  initOLED();
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
    Serial.println("GPS 已开启");

  } else {
    Serial2.end();
    digitalWrite(VGNSS_CTRL, HIGH);
    digitalWrite(GPS_ANT_EN, LOW);
    Serial.println("GPS 已关闭");
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
  if (gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2025) {

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
    Serial.print("✅GPS成功设置一次时间");
    if (diff >= 0) {
      Serial.print("，时间调快了 ");
    } else {
      Serial.print("，时间调慢了 ");
      diff = -diff;
    }
    long long diff_sec = diff / 1000000;
    long long minutes = diff_sec / 60;
    long long seconds = diff_sec % 60;
    long long millis = (diff % 1000000) / 1000;

    Serial.print(minutes);
    Serial.print("分");
    Serial.print(seconds);
    Serial.print("秒");
    Serial.print(millis);
    Serial.println("毫秒");
  }
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
  if (gps.satellites.value() < 4)
    return false;
  // 3. HDOP 小于 3.0（可根据需求调整）
  if (gps.hdop.value() <= 3.0)
    return false;
  // 4. 数据不陈旧（age < 2 秒）
  if (gps.location.age() > 2000)
    return false;
  // 5. 可选：排除 0,0（若认为无效）
  if (gps.location.lat() <= 1 && gps.location.lng() <= 1)
    return false;
  Serial.println("1 ");
  // 6. ★ 新增：受时成功判断（时间必须有效）
  if (!gps.time.isValid())
    return false; // 时间有效
  Serial.println("2 ");
  // 可选：也检查日期有效性
  if (!gps.date.isValid())
    return false;
  Serial.println("3 ");
  // 可选：检查时间数据是否也新鲜（通常与位置同步）
  if (gps.time.age() > 2000)
    return false;
  Serial.println("4 ");

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
  Serial.printf("当前设备编号: %012llX\n", currentId);
  int index = getDevicesIdx();
  if (index != -1) {
    String syname = "vx-x";
#if defined(WIFI_LORA_32_V3)
    syname = "v3-" + String(index);
#endif
#if defined(WIFI_LORA_32_V4)
    syname = "v4-" + String(index);
#endif
    Serial.println("设备认证成功，设备名为: " + syname);
    return syname;
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
#if defined(WIFI_LORA_32_V3)
    return "v3-x";
#endif
    return "v4-x";
  }
}
BLECallbacks initBLEFun(String deviceName, BLEServerCallbacks *serverCallbacks,
                        BLECharacteristicCallbacks *charCallbacks) {

  BLECallbacks cbs;

  BLEDevice::init("牛羊GPS" + deviceName + "-" + (LORA_FREQ / 1000000));
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
  Serial.println("✅ 初始化蓝牙完成");

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

// 安全更新（您自己调用）
long long mathTimeDiffms(int year, int mon, int day, int h, int m, int s,
                         int ms) {
  struct tm t = {0};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
  t.tm_isdst = 0;
  time_t new_sec = mktime(&t);

  struct timeval now_tv;
  gettimeofday(&now_tv, NULL);
  time_t now_sec = now_tv.tv_sec;
  long now_usec = now_tv.tv_usec;
  long now_ms = now_usec / 1000;
  // 计算偏差（毫秒）
  long long new_ms_total = (long long)new_sec * 1000LL + ms;
  long long now_ms_total = (long long)now_sec * 1000LL + now_ms;
  long long diff_ms = new_ms_total - now_ms_total;

  Serial.print("时间偏差 (new - current): ");
  if (diff_ms >= 0) {
    Serial.print("+");
  }
  Serial.print(diff_ms / 1000);
  Serial.print("s ");
  Serial.print(diff_ms % 1000);
  Serial.println("ms");

  return diff_ms;
}

String readBatteryEndStr(String deviceName) {
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

  return outStr;
}

// 从LoRa对时信息设置时间2026/07/14 23:23:10.513
long long mathTimeDiffmstimeFromLora(String timeStr) {
  int year, month, day, hour, minute, second, millis = 0;
  // 尝试解析带毫秒
  if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d.%d", &year, &month, &day,
             &hour, &minute, &second, &millis) == 7) {
    // 解析成功，millis已赋值
  } else if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d", &year, &month, &day,
                    &hour, &minute, &second) == 6) {
    millis = 0; // 无毫秒
  } else {
    Serial.print("❌ LoRa对时解析失败: ");
    Serial.println(timeStr);
    return 0;
  }
  return mathTimeDiffms(year, month, day, hour, minute, second, millis);
}

bool haveRightTime() {
  time_t now;
  struct tm t;
  time(&now);
  gmtime_r(&now, &t); // 使用 gmtime_r 得到 UTC，再手动加 8 小时

  return (t.tm_year + 1900) > 2025;
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

// 通过时间戳（毫秒）设置系统时间
void setTimeFromTimestamp(long long epochMs) {
  struct timeval new_tv;
  new_tv.tv_sec = (time_t)(epochMs / 1000LL);
  new_tv.tv_usec = (suseconds_t)((epochMs % 1000LL) * 1000);
  settimeofday(&new_tv, NULL);
}

// 将毫秒时间戳转为可读时间并打印
void printTimestampMs(long long epochMs, const char* label) {
  time_t sec = (time_t)(epochMs / 1000LL);
  int ms = (int)(epochMs % 1000LL);
  struct tm t;
  localtime_r(&sec, &t);
  Serial.printf("%s%4d/%d/%d %02d:%02d:%02d.%03d\n",
                label,
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec, ms);
}

// 将毫秒差值打印为 N小时N分N秒N毫秒
void printDurationMs(long long diffMs, const char* label) {
  bool negative = diffMs < 0;
  if (negative) {
    diffMs = -diffMs;
  }
  long long hours = diffMs / 3600000LL;
  long long minutes = (diffMs % 3600000LL) / 60000LL;
  long long seconds = (diffMs % 60000LL) / 1000LL;
  long long millis = diffMs % 1000LL;
  Serial.printf("%s%s%lld小时%lld分%lld秒%lld毫秒\n",
                label, negative ? "-" : "",
                hours, minutes, seconds, millis);
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
    Serial.print("❌ LoRa对时解析失败: ");
    Serial.println(timeStr);
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

// 判断是否有有效时间
bool hasValidTime() { return syncedEpoch > 0; }

void initPanRadio(RadioEvents_t *radioEvents, int txPower) {

  Radio.Init(radioEvents);
  Radio.SetChannel(LORA_FREQ);
  Serial.print("✅ 当前lora频段");
  Serial.print(LORA_FREQ);
  Serial.print(" 发射功率");
  Serial.println(txPower);
  Radio.SetRxConfig(MODEM_LORA, LORA_BW, LORA_SF, LORA_CR, 0, PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, false);

  // 发送参数配置
  Radio.SetTxConfig(MODEM_LORA, txPower, 0, LORA_BW, LORA_SF, LORA_CR,
                    PREAMBLE_LENGTH, false, true, 0, 0, false, 1000);

  Serial.println("✅ LoRa 初始化完成");
}