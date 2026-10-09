#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"

// ================= WiFi AP 配置 =================
const char* ap_ssid = "ESP32_Ambient";
const char* ap_password = "12345678";
IPAddress apIP(192, 168, 4, 1);

WebServer server(80);

// ================= 数据发送串口 =================
#define TX_PIN 17
#define RX_PIN 16

// ================= CAN 配置 =================
#define CAN_TX GPIO_NUM_5
#define CAN_RX GPIO_NUM_4
bool can_ready = false;

void initCAN() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    if (twai_start() == ESP_OK) {
      can_ready = true;
      Serial.println("[CAN] TWAI 总线启动成功 (500kbps)");
    }
  } else {
    Serial.println("[CAN] TWAI 启动失败");
  }
}

// ================= 网页首页 =================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 氛围灯 & CAN 控制</title>";
  html += "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:16px}";
  html += ".card{background:#222;border:1px solid #333;border-radius:8px;padding:12px;margin:8px 0}";
  html += "h3{margin:0 0 8px;color:#0fa}";
  html += "input{width:100%;padding:6px;margin:4px 0;background:#000;color:#0fa;border:1px solid #444}";
  html += "button{width:100%;padding:8px;background:#0fa;color:#000;border:0;border-radius:6px;font-weight:bold}";
  html += ".log{background:#000;height:160px;overflow:auto;padding:8px;margin-top:12px;font-size:12px;color:#0f0}";
  html += "</style></head><body>";
  html += "<h2>ESP32 设置面板 (28项)</h2>";

  // 28项动态生成
  for (int i = 1; i <= 28; i++) {
    String name = (i == 1) ? "氛围灯主控制" : "CAN控制 " + String(i);
    String defVal = (i == 1) ? "#00ffa0" : "0x" + String(0x100 + i - 1, HEX);
    html += "<div class='card'><h3>" + String(i) + ". " + name + "</h3>";
    html += "<input id='val_" + String(i) + "' value='" + defVal + "' placeholder='参数值'>";
    html += "<button onclick='send(" + String(i) + ")'>测试发送 (TX GPIO17)</button></div>";
  }

  html += "<div class='log' id='log'></div>";
  html += "<script>";
  html += "function slog(m){var l=document.getElementById('log');l.innerHTML+='['+new Date().toLocaleTimeString()+'] '+m+'<br>';l.scrollTop=l.scrollHeight;}";
  html += "function send(id){var v=document.getElementById('val_'+id).value;";
  html += "slog('发送 ID:'+id+' VAL:'+v);";
  html += "fetch('/api/send?id='+id+'&val='+encodeURIComponent(v)).then(r=>r.text()).then(d=>slog('ESP32:'+d));}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

// ================= 接收网页指令 =================
void handleSend() {
  int id = server.arg("id").toInt();
  String val = server.arg("val");

  // 1. 通过 GPIO17 (Serial2) 物理发送
  String frame = "CMD:ID=" + String(id) + " VAL=" + val + "\n";
  Serial2.print(frame);

  // 2. 如果 CAN 已就绪且是 CAN 项，也通过 TWAI 发一帧
  if (can_ready && id > 1) {
    twai_message_t msg;
    msg.identifier = 0x100 + id - 1;
    msg.extd = 0;
    msg.rtr = 0;
    msg.data_length_code = 8;
    uint8_t d[8] = {0};
    d[0] = id; d[1] = 0xAA; d[2] = 0xBB; d[3] = 0xCC;
    memcpy(msg.data, d, 8);
    twai_transmit(&msg, pdMS_TO_TICKS(100));
    Serial.println("[CAN] 已发送帧 ID=0x" + String(msg.identifier, HEX));
  }

  Serial.println("[TX] GPIO17 发送: " + frame);
  server.send(200, "text/plain", "OK - " + frame);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("===== ESP32 Ambient+CAN Web 启动 =====");

  // 启动 WiFi AP
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ap_ssid, ap_password, 1, 0, 8);
  Serial.printf("AP: %s  URL: http://%s\n", ap_ssid, apIP.toString().c_str());

  // 初始化 CAN
  initCAN();

  // 网页路由
  server.on("/", handleRoot);
  server.on("/api/send", handleSend);

  server.begin();
  Serial.println("Web 服务器已启动");
}

void loop() {
  server.handleClient();
}
