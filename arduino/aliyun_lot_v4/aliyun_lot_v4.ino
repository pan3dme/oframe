#include <WiFi.h>

static WiFiClient espClient;
#include <ArduinoJson.h>

#include <AliyunIoTSDK.h>
AliyunIoTSDK iot;

 

#define PRODUCT_KEY "iq6659GBxxY"
#define DEVICE_NAME "pythontiktok"
#define DEVICE_SECRET "234810429507b6a8bdc24e7fe752b09b"
#define REGION_ID "cn-shanghai"

#define WIFI_SSID "yangchang"
#define WIFI_PASSWD "13787501167"


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
int skipNum=0;
unsigned long lastMsMain = 0;
void loop() {
  AliyunIoTSDK::loop();
  if (millis() - lastMsMain >= 5000) {
    lastMsMain = millis();
    // 发送事件到阿里云平台
    // AliyunIoTSDK::sendEvent("xxx");
    // 发送模型属性到阿里云平台
    
    AliyunIoTSDK::send("total", skipNum++);
    AliyunIoTSDK::send("gpsinfo", "111,222,333,444");
  }
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
