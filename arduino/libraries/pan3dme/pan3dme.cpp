 
#include "pan3dme.h"


#if defined(WIFI_LORA_32_V4)
SSD1306Wire factory_display_my(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);  // addr , freq , i2c group , resolution , rst
#endif

 

bool initFinish = false;

  
// 设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
  0x248B9C697090,  // v4    1
  0x6809A21B5BF8,  // v4    2
  0x8442AAAC85D8,  // v3    3
  0x301BA21B5BF8,  // v4    4
  0x0C46AAAC85D8,  // v3    5
  0xB4E00404A7AC,  // v3    6
  0xF89A3604A7AC,  // v3    7
  0x9875555        // 等待添加
};

 
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
      pinMode(Vext, OUTPUT);  //36
      digitalWrite(Vext, LOW);
      pinMode(RST_OLED, OUTPUT);  //21
      digitalWrite(RST_OLED, HIGH);
      delay(1);
      digitalWrite(RST_OLED, LOW);
      delay(1);
      digitalWrite(RST_OLED, HIGH);
      delay(1);

      factory_display_my.init();
      factory_display_my.setFont(ArialMT_Plain_16);  // 你要的 10号 16
      // factory_display_my.flipScreenVertically();
    #endif
  }
}

void initPanGPS() {
  pinMode(VGNSS_CTRL, OUTPUT);
  digitalWrite(VGNSS_CTRL, LOW);  // 开启GPS电源
  pinMode(GPS_ANT_EN, OUTPUT);
  digitalWrite(GPS_ANT_EN, HIGH);  // 开启天线供电
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS 已启动");
}


// 设备ID认证 (根据MAC地址生成设备名)
String makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  int index = -1;
  for (size_t i = 0; i < sizeof(allowedDevices) / sizeof(allowedDevices[0]); ++i) {
    if (currentId == allowedDevices[i]) {
      index = static_cast<int>(i);
      break;
    }
  }
  if (index != -1) {
    String syname="vx-x";
    #if defined(WIFI_LORA_32_V3)  
      syname="v3-" + String(index);
    #endif
    #if defined(WIFI_LORA_32_V4)
       syname="v4-" + String(index);
    #endif
    Serial.println("设备认证成功，设备名为: " +syname);
    return syname;
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
    #if defined(WIFI_LORA_32_V3)  
      return "v3-x";
    #endif
      return "v4-x";
  }
}
BLECallbacks initBLEFun(String deviceName,BLEServerCallbacks *serverCallbacks,BLECharacteristicCallbacks *charCallbacks) {
  
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
void showDisplayBy4Area(String a, String b, String c, String d) {

  #if defined(WIFI_LORA_32_V4)
    initOLED();
    factory_display_my.clear();
    factory_display_my.drawString(0, 0, a);  // 行1
    factory_display_my.drawString(0, 16, b);    // 行2
    factory_display_my.drawString(0, 32, c);  // 行4
    factory_display_my.drawString(0, 48, d);      // 行3
    factory_display_my.display();
  #endif
}

// 初始化WiFi并同步网络时间
// 初始化WiFi并同步网络时间 (获取后自动断开以省电)
void initLibWifi(struct tm timeinfo) {
  // 如果已经执行过一次对时并断开，则直接返回，不再连接
 
  const char *ssid = "yangchang";
  const char *password = "13787501167";

  Serial.print("正在连接 WiFi");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  int skipNum = 0;

  // 等待连接或超时(10秒)
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    openLedByNum(1, 500);
    Serial.print(".");
    skipNum++;
    showDisplayBy4Area("wifi connect" + String(skipNum), "", "", "");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi 连接成功！");

    // 配置时区和NTP服务器
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    Serial.println("正在同步网络时间...");

    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 50) {
      delay(100);
      retry++;
    }

    if (retry < 50) {
      Serial.println("✅ 网络时间获取成功！");
      // --- 关键修改：获取成功后，断开WiFi ---
      WiFi.disconnect(true);  // true 表示从闪存中删除配置（可选），false 则保留配置
      WiFi.mode(WIFI_OFF);    // 强制关闭 WiFi 模块射频
      Serial.println("📶 WiFi 已关闭以省电");
    } else {
      Serial.println("❌ 获取网络时间失败！");
    }
  } else {
    Serial.println("\n⏰ WiFi 连接超时（10秒），跳过网络对时...");
  }

 
}

 