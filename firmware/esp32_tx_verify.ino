#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// WiFi AP 配置
const char* ap_ssid = "ESP32_Ambient";
const char* ap_password = "12345678";

// 硬件串口配置 (TX=GPIO17, RX=GPIO16)
#define TX_PIN 17
#define RX_PIN 16

AsyncWebServer server(80);

// 模拟的 28 项设置数据
String getSettingsJson() {
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();
  
  for(int i=1; i<=28; i++) {
    JsonObject item = array.createNestedObject();
    item["id"] = i;
    item["name"] = "设置项 " + String(i);
    item["type"] = (i % 3 == 0) ? "can" : "ambient";
    item["value"] = (i % 3 == 0) ? "0x" + String(100 + i, HEX) : "#00ff00";
    item["extra"] = (i % 3 == 0) ? "DLC:8, Target:STM32" : "GRB";
  }
  String output;
  serializeJson(doc, output);
  return output;
}

// 生成网页前端 (包含 28 项卡片)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32 环境光与CAN测试</title>
  <style>
    body { font-family: sans-serif; background: #f4f4f4; margin: 0; padding: 20px; }
    .container { max-width: 800px; margin: auto; }
    .card { background: #fff; padding: 15px; margin-bottom: 10px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    .row { display: flex; gap: 10px; margin-top: 10px; }
    input { flex: 1; padding: 8px; border: 1px solid #ccc; border-radius: 4px; }
    button { background: #007bff; color: #fff; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; margin-top: 10px; width: 100%; }
    button:hover { background: #0056b3; }
    #log { background: #222; color: #0f0; padding: 10px; height: 150px; overflow-y: scroll; margin-top: 20px; border-radius: 8px; font-family: monospace; }
  </style>
</head>
<body>
  <div class="container">
    <h2>ESP32 网页端设置验证</h2>
    <div id="items"></div>
    <div id="log"></div>
  </div>
<script>
  let items = [];
  fetch('/api/settings').then(r => r.json()).then(data => {
    items = data;
    const container = document.getElementById('items');
    items.forEach(item => {
      const div = document.createElement('div');
      div.className = 'card';
      div.innerHTML = `
        <h3>${item.id}. ${item.name}</h3>
        <div class="row">
          <input placeholder="值/参数" value="${item.value}" id="val_${item.id}">
          <input placeholder="附加参数" value="${item.extra}" id="extra_${item.id}">
        </div>
        <button onclick="send(${item.id})">📤 测试发送 (从TX引脚发出)</button>
      `;
      container.appendChild(div);
    });
  });

  function log(msg) {
    const el = document.getElementById('log');
    el.innerHTML += `[${new Date().toLocaleTimeString()}] ${msg}<br>`;
    el.scrollTop = el.scrollHeight;
  }

  function send(id) {
    const item = items.find(i => i.id === id);
    const val = document.getElementById(`val_${id}`).value;
    const extra = document.getElementById(`extra_${id}`).value;
    
    const payload = { id: id, name: item.name, type: item.type, value: val, extra: extra, ts: Date.now() };
    log(`发送: ${item.name} → ${val}`);
    
    fetch('/api/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    .then(r => r.json())
    .then(d => {
      if(d.ok) log(`✅ ESP32已通过TX发出: ${d.sent}`);
      else log(`❌ 发送失败`);
    })
    .catch(e => log(`❌ 错误: ${e}`));
  }
</script>
</body>
</html>
)rawliteral";

void setup() {
  // 初始化硬件串口 (TX=17, RX=16)
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // 初始化调试串口 (USB)
  Serial.begin(115200);
  Serial.println("ESP32 TX 验证固件启动...");

  // 启动 LittleFS (用于存放网页)
  if(!LittleFS.begin(true)) {
    Serial.println("LittleFS 挂载失败");
  } else {
    // 将网页写入文件系统
    File file = LittleFS.open("/index.html", "w");
    if(file) {
      file.print(index_html);
      file.close();
    }
  }

  // 启动 WiFi AP
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("AP Started: " + String(ap_ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());

  // 路由：首页
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  // 路由：获取设置列表
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getSettingsJson());
  });

  // 路由：接收发送指令并物理输出
  server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest *request){
    String body = request->arg("plain");
    DynamicJsonDocument doc(512);
    deserializeJson(doc, body);
    
    String name = doc["name"];
    String val = doc["value"];
    String extra = doc["extra"];
    String type = doc["type"];
    
    // 组帧并通过 TX 引脚物理发送
    String frame = "CMD:" + name + " " + val + " (" + extra + ")|TYPE:" + type + "|VAL:" + val;
    Serial2.println(frame);
    
    // 调试串口也打印
    Serial.println("[api] send -> " + frame);
    
    DynamicJsonDocument resDoc(128);
    resDoc["ok"] = true;
    resDoc["sent"] = frame;
    String res;
    serializeJson(resDoc, res);
    request->send(200, "application/json", res);
  });

  server.begin();
}

void loop() {}
