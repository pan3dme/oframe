#include <WiFi.h>


#include <ArduinoJson.h>

#include <AliyunIoTSDK.h>



WiFiClient espClient;
AliyunIoTSDK iot;



#define PRODUCT_KEY "iq66cDleQPs"
#define DEVICE_NAME "wifi_rola_v4_1001"
#define DEVICE_SECRET "7945f515736e7e0ae98289db07525d95"
#define REGION_ID "cn-shanghai"

#define WIFI_SSID "yangchang"
#define WIFI_PASSWD "13787501167"


// 外部硬件驱动函数
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String a, String b, String c, String d);
String displayBuf[4] = { "", "", "", "" };


String deviceName = "v4-x";

// 设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
  0x248B9C697090,  // v4
  0x6809A21B5BF8,  // v4
  0x8442AAAC85D8,  // v3
  0x301BA21B5BF8,  // v4
  0x0C46AAAC85D8,  // v3
  0x9875555        // 第2个设备
};
// 设备ID认证 (根据MAC地址生成设备名)
void makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  int index = -1;
  for (int i = 0; i < sizeof(allowedDevices) / 8; i++) {  // 修正：计算数组长度
    if (currentId == allowedDevices[i]) {
      index = i;
      break;
    }
  }
  if (index != -1) {
    deviceName = "v4-" + String(index);
    Serial.println("设备认证成功，设备名为: " + deviceName);
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
  }
}
#define DEVICE_NUM    4    //FEM总电源
String deviceNameArr[4] = { "v3-1", "v3-2", "v3-3", "v3-4" };
 
// 用于存储每台设备的当前GPS坐标
struct DeviceGps {
  float lat;
  float lon;
};
DeviceGps gpsData[DEVICE_NUM];

bool isGpsInitialized = false;  // 标记是否已初始化过基础GPS

// 初始化GPS基准点 (第一次发送时的附近0.001)
void initSimulatedGps() {
  if (!isGpsInitialized) {
    float baseLat = 26.529950;
    float baseLon = 109.390224;
//  26.530178, 109.394192
    for (int i = 0; i < DEVICE_NUM; i++) {
 
      gpsData[i].lat = baseLat + ((float)rand() / RAND_MAX * 0.008 - 0.004);
      gpsData[i].lon = baseLon + ((float)rand() / RAND_MAX * 0.008 - 0.004);
    }
    isGpsInitialized = true;
    Serial.println("[模拟] GPS初始坐标生成完毕");
  }
}

void wifiInit(const char *ssid, const char *passphrase) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, passphrase);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("WiFi not Connect");
  }
  Serial.println("Connected to AP");
}

void setup() {
  Serial.begin(115200);
  initSimulatedGps();
  makeDivceName();
  wifiInit(WIFI_SSID, WIFI_PASSWD);

  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.ntsc.ac.cn");  // 同步阿里云NTP时间
  Serial.print("等待时间同步");
  while (time(nullptr) < 1000000000) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n时间同步完成！");

  AliyunIoTSDK::begin(espClient, PRODUCT_KEY, DEVICE_NAME, DEVICE_SECRET, REGION_ID);

  // 绑定属性回调
  AliyunIoTSDK::bindData("PowerSwitch", powerCallback);
}
int skipNum = 0;
unsigned long lastMsMain = 0;
String sendStr = "";
void loop() {
  AliyunIoTSDK::loop();

  openLedByNum(1, 500);
  if (millis() - lastMsMain >= 5000) {
    lastMsMain = millis();
    openLedByNum(5, 50);




    int randomIndex = rand() % DEVICE_NUM;

    // String selectedDevice = deviceNameArr[2];
    // 2. 在上一次发送的GPS信息附近 ±0.0001 浮动
    gpsData[randomIndex].lat += ((float)rand() / RAND_MAX * 0.0002 - 0.0001);
    gpsData[randomIndex].lon += ((float)rand() / RAND_MAX * 0.0002 - 0.0001);
    // 格式化保留6位小数
    char latStr[10], lonStr[10];
    dtostrf(gpsData[randomIndex].lat, 8, 6, latStr);
    dtostrf(gpsData[randomIndex].lon, 8, 6, lonStr);

    sendStr = "1|"+deviceNameArr[randomIndex]+"|"+ String(latStr) + "," + String(lonStr)+"|"+deviceName;

    Serial.println(sendStr);

    AliyunIoTSDK::send("lorainfo", const_cast<char *>(sendStr.c_str()));

    displayBuf[0] =deviceNameArr[randomIndex];
    displayBuf[1] = String(latStr);
    displayBuf[2] = String(lonStr);
    displayBuf[3] = String(deviceName);
    
  }

  showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);
}


void powerCallback(JsonVariant p) {
  Serial.println("powerCallback");
  int isOpen = p["isOpen"];
  if (isOpen == 1) {
    //
  } else {
    //
  }
}
