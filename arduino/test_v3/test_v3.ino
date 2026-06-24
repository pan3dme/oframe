#include <WiFi.h>
#include "esp_wifi.h"

const char* ssid = "yangchang";
const char* password = "13787501167";
//AT+CDKEY=1528E72CBAE47D93189C6D45E76D80ED
void setup() {
  Serial.begin(115200);
  esp_wifi_set_max_tx_power(WIFI_POWER_8_5dBm); 
  delay(1000);
  Serial.println("=== S3 WiFi测试启动 ===");

  // 清空历史WiFi存储
  WiFi.disconnect(true, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  delay(500);

  Serial.print("开始连接WiFi: ");
  Serial.println(ssid);
 
WiFi.begin(ssid, password );
 
  int waitCount = 0;
  const int maxWait = 40;
  while (WiFi.status() != WL_CONNECTED && waitCount < maxWait)
  {
    delay(500);
    Serial.print(".");
    yield(); // 强制释放CPU给空闲任务，喂看门狗，防止S3崩溃重启
    waitCount++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("\n✅ WiFi连接成功，IP：%s\n", WiFi.localIP().toString().c_str());
  }
  else
  {
    Serial.printf("\n❌ WiFi连接失败，状态码：%d\n", WiFi.status());
  }
}

void loop() {
  delay(1000);
  yield();
}