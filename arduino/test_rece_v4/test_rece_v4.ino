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
    loraRadio.setFrequency(868.0);        // 匹配 ETSI_868: 868MHz
    loraRadio.setSpreadingFactor(9);       // 匹配 DEFAULT_SF: SF9
    loraRadio.setBandwidth(125.0);         // 匹配 DEFAULT_BW: BW125
    loraRadio.setCodingRate(1);            // 匹配 DEFAULT_CR: CR4/5 (RadioLib中1=4/5)
    loraRadio.setPreambleLength(8);
    loraRadio.setSyncWord(0x12);           // 标准 LoRa 私有 sync word
    loraRadio.setCRC(false);               // PAN3029 LoRa模式下 CRC_OFF

    // 隐式头模式，payload 长度匹配 TX_LEN=10
    loraRadio.explicitHeader();

    Serial.println("RX ready | 868MHz SF9 BW125 CR4/5 SW:0x12 CRC OFF ImplicitHeader(10)");
}
void loop() {
    uint8_t rxBuffer[10];
    int ret = loraRadio.receive(rxBuffer, sizeof(rxBuffer), 2000);

    if (ret == RADIOLIB_ERR_NONE) {
        int len = loraRadio.getPacketLength();
        if (len > (int)sizeof(rxBuffer)) {
            len = sizeof(rxBuffer);
        }

        Serial.print("OK | RSSI: ");
        Serial.print(loraRadio.getRSSI());
        Serial.print(" | SNR: ");
        Serial.print(loraRadio.getSNR());
        Serial.print(" | Len: ");
        Serial.print(len);
        Serial.print(" | Data: ");

        for (int i = 0; i < len; i++) {
            uint8_t v = (uint8_t)rxBuffer[i];
            if (v < 0x10) {
                Serial.print('0');
            }
            Serial.print(v, HEX);
            Serial.print(' ');
        }
        Serial.println();

    } else if (ret == RADIOLIB_ERR_RX_TIMEOUT) {
        Serial.println("RX timeout, listening...");

    } else {
        Serial.print("Error: ");
        Serial.println(ret);
    }
}