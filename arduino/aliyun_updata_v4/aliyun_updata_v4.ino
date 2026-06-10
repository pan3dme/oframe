#include <WiFi.h>


#include <ArduinoJson.h>

#include <AliyunIoTSDK.h>



WiFiClient espClient;
AliyunIoTSDK iot;



#define PRODUCT_KEY "iq6659GBxxY"
#define DEVICE_NAME "pythontiktok"
#define DEVICE_SECRET "234810429507b6a8bdc24e7fe752b09b"
#define REGION_ID "cn-shanghai"

#define WIFI_SSID "yangchang"
#define WIFI_PASSWD "13787501167"


// 外部硬件驱动函数
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String a, String b, String c, String d);
String displayBuf[4] = { "", "", "", "" };

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
    // 发送事件到阿里云平台
    // AliyunIoTSDK::sendEvent("xxx");
    // 发送模型属性到阿里云平台

    sendStr = "1|v3-1|26.000000,109.360000|" + String(skipNum);
    Serial.println(sendStr);

    AliyunIoTSDK::send("total", skipNum++);
    AliyunIoTSDK::send("gpsinfo", const_cast<char*>(sendStr.c_str()));
    displayBuf[0] = String(skipNum);
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
