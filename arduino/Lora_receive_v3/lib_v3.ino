 
 

// ================= LED 带参数闪烁 =================
void openLedByNum(int count, int delayMs) {
  pinMode(LED, OUTPUT);//35
  for (int i = 0; i < count; i++) {
    digitalWrite(LED, HIGH);
    delay(delayMs);
    digitalWrite(LED, LOW);
    delay(delayMs);
  }
}


// 4 分区显示函数
void showDisplayBy4Area(String loraStatus, String gpsInfo, String sysStatus, String remak) {

 
}

 
