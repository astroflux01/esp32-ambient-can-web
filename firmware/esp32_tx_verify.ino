#include <CAN.h>

#define CAN_TX_PIN 5
#define CAN_RX_PIN 4

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 CAN Test Starting...");
  
  // 设置CAN引脚
  CAN.setPins(CAN_RX_PIN, CAN_TX_PIN);
  
  // 以500kbps启动CAN总线
  if (!CAN.begin(500E3)) {
    Serial.println("CAN init failed!");
    while (1);
  }
  Serial.println("CAN bus ready at 500 kbps");
}

void loop() {
  // 每秒发送一帧CAN数据
  CAN.beginPacket(0x123);
  CAN.write(0xDE);
  CAN.write(0xAD);
  CAN.write(0xBE);
  CAN.write(0xEF);
  CAN.endPacket();
  
  Serial.println("CAN Frame sent");
  delay(1000);
}
