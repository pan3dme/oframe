
#include "HT_SSD1306Wire.h"

// 官方定义引脚

SSD1306Wire factory_display_my(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);  // addr , freq , i2c group , resolution , rst
                                                                        // 运行计数器

bool initFinish = false;


// ================= LED 带参数闪烁 =================
void openLedByNum(int count, int delayMs) {
  initOLED();
  for (int i = 0; i < count; i++) {
    digitalWrite(LED, HIGH);
    delay(delayMs);
    digitalWrite(LED, LOW);
    delay(delayMs);
  }
}


// 开启OLED电源
void VextON(void) {
  pinMode(Vext, OUTPUT);  //36
  digitalWrite(Vext, LOW);
}


// 复位OLED
void displayReset(void) {
  pinMode(RST_OLED, OUTPUT);  //21
  digitalWrite(RST_OLED, HIGH);
  delay(1);
  digitalWrite(RST_OLED, LOW);
  delay(1);
  digitalWrite(RST_OLED, HIGH);
  delay(1);
}

// OLED统一初始化函数（给main.ino调用）
void initOLED() {
  if (!initFinish) {
    initFinish = true;
    pinMode(LED, OUTPUT);  //35
    VextON();
    displayReset();
    factory_display_my.init();
    factory_display_my.setFont(ArialMT_Plain_16);  // 你要的 10号 16
    // factory_display_my.flipScreenVertically();
  }
}




// 4 分区显示函数
void showDisplayBy4Area(String a, String b, String c, String d) {

  // initOLED();
  // factory_display_my.clear();
  // factory_display_my.drawString(0, 0, a);  // 行1
  // factory_display_my.drawString(0, 16, b);    // 行2
  // factory_display_my.drawString(0, 32, c);  // 行4
  // factory_display_my.drawString(0, 48, d);      // 行3
  // factory_display_my.display();
}
