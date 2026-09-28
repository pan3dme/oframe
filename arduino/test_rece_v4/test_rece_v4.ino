#include <RadioLib.h>
#define LORA_NSS    8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13
SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
#define PA_POWER    7
#define PA_EN       2
#define PA_TX_EN    46
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PA_POWER, OUTPUT);
  digitalWrite(PA_POWER, LOW);
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, LOW);
  pinMode(PA_TX_EN, OUTPUT);
  digitalWrite(PA_TX_EN, LOW);
  int initResult = loraRadio.begin();
  if (initResult != RADIOLIB_ERR_NONE) {
    Serial.print("Radio init fail: ");
    Serial.println(initResult);
    while (1);
  }
  loraRadio.setDio2AsRfSwitch(false);
  loraRadio.setFrequency(852.0);
  loraRadio.setSpreadingFactor(11);
  loraRadio.setBandwidth(125.0);
  loraRadio.setCodingRate(1);
  loraRadio.setPreambleLength(8);
  loraRadio.setSyncWord(0x2424);
  loraRadio.setCRC(false);
  loraRadio.implicitHeader(16);
  Serial.println("RX ready | 852MHz SF11 BW125 CR4/5 SW:0x2424 CRC OFF ImplicitHeader(16)");
}
void loop() {
  uint8_t rxBuffer[128];
  int ret = loraRadio.receive(rxBuffer, 16, 2000);
  if (ret == RADIOLIB_ERR_NONE) {
    int len = loraRadio.getPacketLength();
    Serial.print("OK | RSSI: ");
    Serial.print(loraRadio.getRSSI());
    Serial.print(" | SNR: ");
    Serial.print(loraRadio.getSNR());
    Serial.print(" | Len: ");
    Serial.print(len);
    Serial.print(" | Data: ");
    for (int i = 0; i < len; i++) {
      Serial.printf("%02x ", rxBuffer[i]);
    }
    Serial.println();
  } else if (ret == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println("RX timeout, listening...");
  } else {
    Serial.print("Error: ");
    Serial.println(ret);
  }
}
