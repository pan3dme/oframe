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
    loraRadio.setFrequency(868.0);
    loraRadio.setSpreadingFactor(9);
    loraRadio.setBandwidth(125.0);
    loraRadio.setCodingRate(1);
    loraRadio.setPreambleLength(8);
    loraRadio.setSyncWord(0x12);
    loraRadio.setCRC(false);

    // 接收端用了 explicitHeader()，发送端也用显式头模式
    loraRadio.explicitHeader();

    Serial.println("TX ready | 868MHz SF9 BW125 CR4/5 SW:0x12 CRC OFF");
}

void loop() {
    const char *msg = "FUCK 来看看  YOU CAN";
    int len = strlen(msg);

    int ret = loraRadio.transmit((uint8_t *)msg, len);
    if (ret == RADIOLIB_ERR_NONE) {
        Serial.print("TX OK | Len: ");
        Serial.println(len);
    } else {
        Serial.print("TX Error: ");
        Serial.println(ret);
    }

    delay(1000);
}