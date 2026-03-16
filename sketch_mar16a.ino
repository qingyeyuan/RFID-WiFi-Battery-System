#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_SHT31.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <Adafruit_SSD1306.h>

// ================= 引脚定义 =================
// SHT3X 使用 I2C1
#define I2C1_SDA        32
#define I2C1_SCL        33

// RFID
#define SS_PIN          5
#define RST_PIN         27

// 其他
#define BUZZER_PIN      26
#define BATTERY_PIN     34
#define BOOT_PIN        0  // Boot按钮引脚

// OLED显示
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS  0x3C




// 电池参数
const float VOLTAGE_DIVIDER_RATIO = 6.0;
const int   ADC_RESOLUTION        = 4095;
const float BATT_MIN_V            = 2.0;
const float BATT_MAX_V            = 3.1;

// EEPROM地址定义
#define EEPROM_SIZE 128
#define EEPROM_CONFIG_ADDR 0
#define EEPROM_MAGIC 0x55AA

// 配置参数结构体
struct Config {
  uint16_t magic;
  char ssid[32];
  char password[64];
  char server_ip[16];
  int server_port;
  char device_id[32];
  bool configured;
};

Config config;

// ================= 对象实例化 =================
Adafruit_SHT31 sht31(&Wire1);
MFRC522        mfrc522(SS_PIN, RST_PIN);
WiFiClient client;                         // TCP客户端实例
WebServer server(80);                      // Web服务器实例
DNSServer dnsServer;                       // DNS服务器实例
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

// 状态变量
String        lastUID    = "Waiting...";
unsigned long lastUpdate = 0;
unsigned long lastSend   = 0;
unsigned long bootPressTime = 0;
const unsigned long SEND_INTERVAL = 3000;  // 发送间隔（3秒）
const unsigned long BOOT_PRESS_THRESHOLD = 3000;  // Boot按钮长按阈值（3秒）

// ================= 工具函数 =================
void beep(int ms) {
  digitalWrite(BUZZER_PIN, LOW);  // 低电平触发
  delay(ms);
  digitalWrite(BUZZER_PIN, HIGH); // 高电平停止
}

float readBatteryVoltage() {
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(BATTERY_PIN);
    delay(2);
  }
  float pinV = (total / 10.0 / ADC_RESOLUTION) * 1.1;
  return pinV * VOLTAGE_DIVIDER_RATIO;
}

float calcSOC(float voltage) {
  return constrain((voltage - BATT_MIN_V) / (BATT_MAX_V - BATT_MIN_V) * 100.0, 0.0, 100.0);
}

void printStatus();
void sendDataToServer();
void readConfig();
void saveConfig();
void startConfigMode();
void handleRoot();
void handleSaveConfig();
void handleScanWiFi();
String getWiFiListHTML();

// ================= 初始化 =================
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);  // 初始化为高电平（不响）
  
  // 初始化Boot按钮
  pinMode(BOOT_PIN, INPUT);

  // I2C1 → SHT3X（仅需要这一条总线）
  Wire1.begin(I2C1_SDA, I2C1_SCL);

  // SHT3X
  if (!sht31.begin(0x44) && !sht31.begin(0x45)) {
    Serial.println("[ERR] SHT3X 初始化失败，检查 GPIO32/33 接线!");
  } else {
    Serial.println("[OK]  SHT3X 就绪");
  }

  // OLED显示
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("[ERR] OLED 初始化失败，检查 GPIO32/33 接线!");
  } else {
    Serial.println("[OK]  OLED 就绪");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Starting...");
    display.display();
  }

  // SPI + RFID
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();
  Serial.println("[OK]  RFID 就绪");

  // ADC
  analogSetPinAttenuation(BATTERY_PIN, ADC_0db);
  pinMode(BATTERY_PIN, INPUT);

  // EEPROM初始化
  EEPROM.begin(EEPROM_SIZE);
  readConfig();

  // WiFi配置
  if (config.configured) {
    // 尝试连接WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);
    Serial.print("WiFi 连接中");
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[OK]  WiFi 已连接");
    } else {
      Serial.println("\n[--]  离线模式");
      // WiFi连接失败，启动配网模式
      Serial.println("[CFG] WiFi连接失败，启动配网模式");
      startConfigMode();
    }
  } else {
    // 启动配网模式
    Serial.println("[CFG] 未检测到配置，启动配网模式");
    startConfigMode();
  }

  Serial.println("========== 系统就绪 ==========");
}

// ================= 主循环 =================
void loop() {
  // RFID 轮询
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    lastUID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) lastUID += '0';
      lastUID += String(mfrc522.uid.uidByte[i], HEX);
    }
    lastUID.toUpperCase();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    Serial.println(">>> 刷卡 UID: " + lastUID);
    beep(80);

    printStatus();
    lastUpdate = millis();
  }

  // 定时刷新（2秒）
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate >= 2000) {
    lastStatusUpdate = millis();
    printStatus();
    
    // 检查温度和湿度，超过阈值触发警报
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      if (t > 25.0 || h >= 90.0) {
        Serial.println("[ALERT] 温度或湿度超过阈值！");
        beep(200);
        delay(100);
        beep(200);
      }
    }
  }

  // 定时发送数据（10秒）
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();
    sendDataToServer();
  }

  // Web服务器处理
  server.handleClient();
  
  // DNS服务器处理
  dnsServer.processNextRequest();
  
  // Boot按钮检测
  if (digitalRead(BOOT_PIN) == LOW) {  // Boot按钮按下（低电平）
    if (bootPressTime == 0) {
      bootPressTime = millis();
    } else if (millis() - bootPressTime >= BOOT_PRESS_THRESHOLD) {
      // 长按Boot按钮超过阈值，清除配置
      Serial.println("[CFG] 长按Boot按钮，清除配置...");
      
      // 蜂鸣提示
      beep(500);
      delay(500);
      beep(500);
      
      // 清除配置
      config.configured = false;
      strcpy(config.ssid, "");
      strcpy(config.password, "");
      strcpy(config.server_ip, "192.168.1.100");
      config.server_port = 8080;
      strcpy(config.device_id, "25824");
      saveConfig();
      
      // 重启设备
      Serial.println("[CFG] 配置已清除，设备将重启...");
      delay(1000);
      ESP.restart();
    }
  } else {
    // 按钮释放，重置计时
    bootPressTime = 0;
  }
}

// ================= OLED显示更新 =================
void updateOLED() {
  // 读取传感器数据
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (isnan(t)) t = 0.0;
  if (isnan(h)) h = 0.0;

  float volt = readBatteryVoltage();
  float soc  = calcSOC(volt);
  
  // 清除显示
  display.clearDisplay();
  
  // 设置字体大小和颜色
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // 第一行：温度和湿度
  display.setCursor(0, 0);
  display.print("Tem:");
  display.print(t, 1);
  display.print("C");
  display.setCursor(70, 0);
  display.print("Hum:");
  display.print(h, 1);
  display.print("%");
  
  // 第二行：电压和电量
  display.setCursor(0, 16);
  display.print("Volt:");
  display.print(volt, 2);
  display.print("V");
  display.setCursor(70, 16);
  display.print("Batt:");
  display.print(soc, 0);
  display.print("%");
  
  // 第三行：WiFi状态
  display.setCursor(0, 32);
  display.print("WiFi: ");
  display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  
  // 第四行：RFID UID
  display.setCursor(0, 48);
  display.print("UID: ");
  display.print(lastUID.length() > 12 ? lastUID.substring(0, 12) + "..." : lastUID);
  
  // 刷新显示
  display.display();
}

// ================= 串口输出 =================
void printStatus() {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (isnan(t)) t = 0.0;
  if (isnan(h)) h = 0.0;

  float volt = readBatteryVoltage();
  float soc  = calcSOC(volt);

  Serial.println("─────────────────────────");
  Serial.printf("温度: %.1f°C   湿度: %.1f%%\n", t, h);
  Serial.printf("电压: %.2fV   电量: %.0f%%\n", volt, soc);
  Serial.printf("WiFi: %s   UID: %s\n",
                WiFi.status() == WL_CONNECTED ? "ON" : "OFF",
                lastUID.c_str());
  
  // 更新OLED显示
  updateOLED();
}

// ================= TCP数据发送 =================
void sendDataToServer() {
  // 检查WiFi连接状态
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TCP] WiFi未连接，跳过数据发送");
    return;
  }

  // 检查TCP连接状态，如果未连接则尝试连接
  if (!client.connected()) {
    Serial.print("[TCP] 连接服务器 " + String(config.server_ip) + ":" + String(config.server_port) + "...");
    if (client.connect(config.server_ip, config.server_port)) {
      Serial.println("成功");
    } else {
      Serial.println("失败");
      return;
    }
  }

  // 读取传感器数据
  float temperature = sht31.readTemperature();
  float humidity = sht31.readHumidity();
  float voltage = readBatteryVoltage();
  float soc = calcSOC(voltage);

  // 处理无效数据
  if (isnan(temperature)) temperature = 0.0;
  if (isnan(humidity)) humidity = 0.0;

  // 构建JSON数据（按照要求的命名规范）
  String jsonData = "{";
  jsonData += "\"number\": \"" + String(config.device_id) + "\",";
  jsonData += "\"voltage\": \"" + String(voltage, 2) + "\",";
  jsonData += "\"soc\": \"" + String(soc, 0) + "\",";
  jsonData += "\"temp\": \"" + String(temperature, 1) + "\",";
  jsonData += "\"humidity\": \"" + String(humidity, 1) + "\",";
  jsonData += "\"rfid\": \"" + lastUID + "\"";
  jsonData += "}";

  // 发送JSON数据
  client.print(jsonData);
  Serial.println("[TCP] 数据已发送: " + jsonData);
  
  // 等待服务器响应
  unsigned long start = millis();
  while (millis() - start < 1000 && !client.available()) {
    delay(10);
  }
  
  // 读取服务器响应
  if (client.available()) {
    String response = client.readStringUntil('\n');
    Serial.println("[TCP] 服务器响应: " + response);
  }
  
  // 保持连接，不关闭
  // client.stop();
  // Serial.println("[TCP] 连接已关闭");
}

// ================= 配置管理 =================

// 从EEPROM读取配置
void readConfig() {
  EEPROM.get(EEPROM_CONFIG_ADDR, config);
  if (config.magic != EEPROM_MAGIC) {
    // 配置无效，使用默认值
    config.magic = EEPROM_MAGIC;
    strcpy(config.ssid, "");
    strcpy(config.password, "");
    strcpy(config.server_ip, "192.168.1.100");
    config.server_port = 8080;
    strcpy(config.device_id, "25824");
    config.configured = false;
  }
  Serial.println("[CFG] 配置读取完成");
}

// 保存配置到EEPROM
void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_CONFIG_ADDR, config);
  EEPROM.commit();
  Serial.println("[CFG] 配置已保存");
}

// 启动配网模式
void startConfigMode() {
  // 启动AP+STA模式（同时支持热点和扫描）
  const char* ap_ssid = "Device-Config";
  const char* ap_password = "12345678";
  
  Serial.println("[CFG] ===== 启动配网模式 =====");
  
  // 完全重置WiFi状态
  WiFi.disconnect(true); // 断开所有连接并清除配置
  delay(200);
  
  // 关闭WiFi
  WiFi.mode(WIFI_OFF);
  delay(200);
  
  // 设置为AP+STA模式
  WiFi.mode(WIFI_AP_STA);
  delay(300);
  
  // 配置AP热点参数
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  // 配置AP网络
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);
  
  // 启动AP热点
  boolean apStarted = WiFi.softAP(ap_ssid, ap_password, 6, false, 4);
  delay(1000); // 等待AP完全启动
  
  if (apStarted) {
    IPAddress ap_ip = WiFi.softAPIP();
    Serial.printf("[CFG] 配网模式已启动\n");
    Serial.printf("[CFG] 热点名称: %s\n", ap_ssid);
    Serial.printf("[CFG] 热点密码: %s\n", ap_password);
    Serial.printf("[CFG] 配置地址: http://%s\n", ap_ip.toString().c_str());
    Serial.printf("[CFG] 连接数限制: 4\n");
    Serial.printf("[CFG] 信道: 6\n");
    
    // 配置DNS服务器，将所有域名解析到设备IP
    dnsServer.start(53, "*", ap_ip);
    Serial.println("[CFG] DNS服务器已启动");
    
    // 配置Web服务器路由
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSaveConfig);
    server.on("/scan", HTTP_GET, handleScanWiFi);
    // 处理所有其他路径，重定向到根路径
    server.onNotFound([]() {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    });
    server.begin();
    Serial.println("[CFG] Web服务器已启动");
    
    // 蜂鸣提示
    beep(200);
    delay(200);
    beep(200);
    Serial.println("[CFG] ===== 配网模式就绪 =====");
  } else {
    Serial.println("[ERR] 热点启动失败！");
    // 尝试使用不同的信道
    for (int channel = 1; channel <= 13; channel++) {
      Serial.printf("[CFG] 尝试使用信道 %d...\n", channel);
      WiFi.softAPConfig(local_ip, gateway, subnet);
      delay(100);
      if (WiFi.softAP(ap_ssid, ap_password, channel, false, 4)) {
        delay(500);
        IPAddress ap_ip = WiFi.softAPIP();
        Serial.printf("[CFG] 热点已在信道 %d 启动\n", channel);
        Serial.printf("[CFG] 配置地址: http://%s\n", ap_ip.toString().c_str());
        server.begin();
        beep(100);
        delay(100);
        beep(100);
        break;
      }
      delay(200);
    }
  }
}

// 处理根路径请求
void handleRoot() {
  String html;
  html.reserve(4096); // 预分配内存，防止长期运行产生内存碎片
  
  html += "<!DOCTYPE html>";
  html += "<html lang=\"zh-CN\">";
  html += "<head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\">";
  html += "<title>设备配网与配置</title>";
  html += "<style>";
  html += ":root { --primary: #4361ee; --primary-hover: #3a53cc; --bg: #f4f7f6; --text-main: #333; --text-muted: #6c757d; --border: #dee2e6; }";
  html += "body { font-family: system-ui, -apple-system, sans-serif; background-color: var(--bg); color: var(--text-main); margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; box-sizing: border-box; }";
  html += ".card { background: #fff; width: 100%; max-width: 420px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.08); padding: 30px; box-sizing: border-box; }";
  html += ".header { text-align: center; margin-bottom: 25px; }";
  html += "h1 { margin: 0 0 10px 0; font-size: 22px; color: var(--primary); }";
  html += ".subtitle { color: var(--text-muted); font-size: 14px; margin: 0; }";
  html += ".section-title { font-size: 16px; font-weight: 600; margin: 25px 0 15px 0; padding-bottom: 8px; border-bottom: 1px solid var(--border); color: #111; }";
  html += ".form-group { margin-bottom: 16px; position: relative; }";
  html += "label { display: block; margin-bottom: 8px; font-size: 14px; font-weight: 500; color: #444; }";
  html += "input { width: 100%; padding: 12px 14px; border: 1px solid var(--border); border-radius: 8px; box-sizing: border-box; font-size: 15px; transition: all 0.3s; background-color: #fafafa; }";
  html += "input:focus { border-color: var(--primary); outline: none; background-color: #fff; box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.15); }";
  html += ".btn { width: 100%; padding: 12px; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.3s; display: flex; justify-content: center; align-items: center; gap: 8px; }";
  html += ".btn-primary { background-color: var(--primary); color: white; margin-top: 20px; }";
  html += ".btn-primary:hover { background-color: var(--primary-hover); transform: translateY(-1px); }";
  html += ".btn-secondary { background-color: #e9ecef; color: #495057; font-size: 14px; padding: 10px; margin-bottom: 10px; }";
  html += ".btn-secondary:hover { background-color: #dde2e6; }";
  html += ".wifi-list { border: 1px solid var(--border); border-radius: 8px; max-height: 220px; overflow-y: auto; background: #fff; display: none; margin-bottom: 15px; }";
  html += ".wifi-item { padding: 12px 15px; border-bottom: 1px solid #f1f3f5; cursor: pointer; display: flex; justify-content: space-between; align-items: center; transition: background 0.2s; }";
  html += ".wifi-item:last-child { border-bottom: none; }";
  html += ".wifi-item:hover { background-color: #f8f9fa; }";
  html += ".wifi-name { font-weight: 500; font-size: 15px; }";
  html += ".wifi-signal { color: var(--text-muted); font-size: 12px; background: #f1f3f5; padding: 3px 8px; border-radius: 12px; }";
  html += ".loading { text-align: center; padding: 20px; color: var(--text-muted); font-size: 14px; display: flex; flex-direction: column; align-items: center; gap: 10px; }";
  html += ".spinner { width: 22px; height: 22px; border: 3px solid rgba(67, 97, 238, 0.2); border-top-color: var(--primary); border-radius: 50%; animation: spin 0.8s linear infinite; }";
  html += "@keyframes spin { 100% { transform: rotate(360deg); } }";
  html += ".toggle-pwd { position: absolute; right: 14px; top: 38px; cursor: pointer; color: var(--text-muted); font-size: 14px; user-select: none; font-weight: 500; }";
  html += ".toggle-pwd:hover { color: var(--primary); }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class=\"card\">";
  html += "<div class=\"header\">";
  html += "<h1>设备配置</h1>";
  html += "<p class=\"subtitle\">请连接并配置您的设备信息</p>";
  html += "</div>";
  html += "<form action=\"/save\" method=\"post\">";
  
  // Wi-Fi 设置区域
  html += "<div class=\"section-title\">Wi-Fi 网络设置</div>";
  html += "<button type=\"button\" class=\"btn btn-secondary\" onclick=\"scanWiFi()\">";
  html += "<svg style=\"width:16px;height:16px;\" viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M12,21L15.6,16.2C14.6,15.45 13.35,15 12,15C10.65,15 9.4,15.45 8.4,16.2L12,21M12,3C7.95,3 4.21,4.34 1.2,6.6L3,9C5.5,7.12 8.62,6 12,6C15.38,6 18.5,7.12 21,9L22.8,6.6C19.79,4.34 16.05,3 12,3M12,9C9.3,9 6.81,9.89 4.8,11.4L6.6,13.8C8.1,12.67 9.97,12 12,12C14.03,12 15.9,12.67 17.4,13.8L19.2,11.4C17.19,9.89 14.7,9 12,9Z\" /></svg>";
  html += "扫描附近 Wi-Fi";
  html += "</button>";
  html += "<div id=\"wifiList\" class=\"wifi-list\"></div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"ssid\">Wi-Fi 名称 (SSID)</label>";
  html += "<input type=\"text\" id=\"ssid\" name=\"ssid\" placeholder=\"请输入或从上方选择\" value=\"" + String(config.ssid) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"password\">Wi-Fi 密码</label>";
  html += "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"请输入密码\" value=\"" + String(config.password) + "\">";
  html += "<span class=\"toggle-pwd\" onclick=\"togglePwd()\">显示</span>";
  html += "</div>";

  // 服务器设置区域
  html += "<div class=\"section-title\">服务器及设备参数</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"server_ip\">服务器 IP 地址</label>";
  html += "<input type=\"text\" id=\"server_ip\" name=\"server_ip\" placeholder=\"例如: 192.168.1.10\" value=\"" + String(config.server_ip) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"server_port\">服务器端口</label>";
  html += "<input type=\"number\" id=\"server_port\" name=\"server_port\" placeholder=\"例如: 8080\" value=\"" + String(config.server_port) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"device_id\">设备 ID</label>";
  html += "<input type=\"text\" id=\"device_id\" name=\"device_id\" placeholder=\"例如: DEV-001\" value=\"" + String(config.device_id) + "\">";
  html += "</div>";
  
  html += "<button type=\"submit\" class=\"btn btn-primary\">保存配置并重启</button>";
  html += "</form>";
  html += "</div>";

  // JavaScript 脚本
  html += "<script>";
  html += "function scanWiFi() {";
  html += "  const wifiListDiv = document.getElementById('wifiList');";
  html += "  wifiListDiv.style.display = 'block';";
  html += "  wifiListDiv.innerHTML = '<div class=\"loading\"><div class=\"spinner\"></div><span>正在扫描附近网络...</span></div>';";
  html += "  fetch('/scan')";
  html += "    .then(response => response.text())";
  html += "    .then(html => { wifiListDiv.innerHTML = html ? html : '<div class=\"loading\">未找到网络，请重试</div>'; })";
  html += "    .catch(error => { wifiListDiv.innerHTML = '<div class=\"loading\" style=\"color:#e63946;\">扫描失败，请检查设备状态</div>'; });";
  html += "}";
  html += "function selectWiFi(ssid) {";
  html += "  document.getElementById('ssid').value = ssid;";
  html += "  document.getElementById('password').focus();";
  html += "}";
  html += "function togglePwd() {";
  html += "  const pwd = document.getElementById('password');";
  html += "  const btn = document.querySelector('.toggle-pwd');";
  html += "  if (pwd.type === 'password') { pwd.type = 'text'; btn.textContent = '隐藏'; }";
  html += "  else { pwd.type = 'password'; btn.textContent = '显示'; }";
  html += "}";
  html += "</script>";
  html += "</body>";
  html += "</html>";
  
  server.send(200, "text/html", html);
}

// 处理保存配置请求
void handleSaveConfig() {
  // 获取表单数据
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String server_ip = server.arg("server_ip");
  int server_port = server.arg("server_port").toInt();
  String device_id = server.arg("device_id");
  
  // 保存到配置
  ssid.toCharArray(config.ssid, sizeof(config.ssid));
  password.toCharArray(config.password, sizeof(config.password));
  server_ip.toCharArray(config.server_ip, sizeof(config.server_ip));
  config.server_port = server_port;
  device_id.toCharArray(config.device_id, sizeof(config.device_id));
  config.configured = true;
  
  // 保存到EEPROM
  saveConfig();
  
  // 蜂鸣提示
  beep(200);
  
  // 发送美化后的成功响应页面
  String html;
  html.reserve(1500);
  html += "<!DOCTYPE html>";
  html += "<html lang=\"zh-CN\">";
  html += "<head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>配置成功</title>";
  html += "<style>";
  html += "body { font-family: system-ui, -apple-system, sans-serif; background-color: #f4f7f6; margin: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }";
  html += ".card { background: white; padding: 40px 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.08); text-align: center; max-width: 400px; width: 90%; box-sizing: border-box; }";
  html += ".icon { width: 64px; height: 64px; background-color: #e8f5e9; color: #4CAF50; border-radius: 50%; display: flex; align-items: center; justify-content: center; margin: 0 auto 20px auto; }";
  html += "h1 { color: #2e3d49; margin: 0 0 12px 0; font-size: 24px; }";
  html += "p { color: #6c757d; margin: 0; line-height: 1.6; font-size: 15px; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class=\"card\">";
  html += "<div class=\"icon\">";
  html += "<svg style=\"width:36px;height:36px;\" viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M21,7L9,19L3.5,13.5L4.91,12.09L9,16.17L19.59,5.59L21,7Z\" /></svg>";
  html += "</div>";
  html += "<h1>配置保存成功</h1>";
  html += "<p>设备即将重启并尝试连接到您配置的 Wi-Fi 网络。<br>您可以现在关闭此页面。</p>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  server.send(200, "text/html", html);
  
  // 延迟后重启
  delay(3000);
  ESP.restart();
}

// 处理WiFi扫描请求
void handleScanWiFi() {
  String wifiListHTML = getWiFiListHTML();
  server.send(200, "text/html", wifiListHTML);
}

// 获取WiFi列表HTML
String getWiFiListHTML() {
  String html = "";
  
  // 扫描附近的WiFi网络
  Serial.println("[WiFi] 开始扫描附近WiFi...");
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    // 未找到任何WiFi网络
    html += "<div class=\"loading\">未找到附近的WiFi网络</div>";
    Serial.println("[WiFi] 未找到任何WiFi网络");
  } else {
    // 按信号强度排序（从强到弱）
    int indices[n];
    for (int i = 0; i < n; i++) {
      indices[i] = i;
    }
    
    // 简单的冒泡排序，按信号强度降序排列
    for (int i = 0; i < n - 1; i++) {
      for (int j = 0; j < n - i - 1; j++) {
        if (WiFi.RSSI(indices[j]) < WiFi.RSSI(indices[j + 1])) {
          int temp = indices[j];
          indices[j] = indices[j + 1];
          indices[j + 1] = temp;
        }
      }
    }
    
    // 生成WiFi列表HTML
    for (int i = 0; i < n; i++) {
      int idx = indices[i];
      String ssid = WiFi.SSID(idx);
      int rssi = WiFi.RSSI(idx);
      
      // 跳过空SSID
      if (ssid.length() == 0) continue;
      
      // 计算信号强度百分比（RSSI范围通常是-30到-90 dBm）
      int signalPercent = map(rssi, -90, -30, 0, 100);
      signalPercent = constrain(signalPercent, 0, 100);
      
      // 转义SSID中的特殊字符
      String escapedSSID = ssid;
      escapedSSID.replace("\"", "&quot;");
      escapedSSID.replace("'", "&#39;");
      
      html += "<div class=\"wifi-item\" onclick=\"selectWiFi('" + escapedSSID + "')\">";
      html += "<span class=\"wifi-name\">" + ssid + "</span>";
      html += "<span class=\"wifi-signal\">" + String(signalPercent) + "% (" + String(rssi) + " dBm)</span>";
      html += "</div>";
      
      Serial.printf("[WiFi] %d. SSID: %s, RSSI: %d dBm (%d%%)\n", i + 1, ssid.c_str(), rssi, signalPercent);
    }
    
    Serial.printf("[WiFi] 扫描完成，共找到 %d 个WiFi网络\n", n);
  }
  
  // 清理扫描结果
  WiFi.scanDelete();
  
  return html;
}