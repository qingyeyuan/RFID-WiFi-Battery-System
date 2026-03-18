#include <EEPROM.h>
#include "../include/network.h"
#include "../include/utils.h"
#include <WiFi.h>

// 网络对象指针
WebServer* server = nullptr;
DNSServer* dnsServer = nullptr;

/**
 * @brief 初始化网络
 */
void initNetwork() {
  // 创建网络对象
  server = new WebServer(80);
  dnsServer = new DNSServer();
  
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
}

/**
 * @brief 启动配网模式
 */
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
    dnsServer->start(53, "*", ap_ip);
    Serial.println("[CFG] DNS服务器已启动");
    
    // 配置Web服务器路由
    server->on("/", HTTP_GET, handleRoot);
    server->on("/save", HTTP_POST, handleSaveConfig);
    server->on("/scan", HTTP_GET, handleScanWiFi);
    // 处理所有其他路径，重定向到根路径
    server->onNotFound([]() {
      server->sendHeader("Location", "/", true);
      server->send(302, "text/plain", "");
    });
    server->begin();
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
        server->begin();
        beep(100);
        delay(100);
        beep(100);
        break;
      }
      delay(200);
    }
  }
}

/**
 * @brief 处理网络请求
 */
void handleNetwork() {
  if (server != nullptr) server->handleClient();
  if (dnsServer != nullptr) dnsServer->processNextRequest();
}

/**
 * @brief 处理根路径请求
 */
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
  
  // 分流电阻值设置
  html += "<div class=\"form-group\">";
  html += "<label for=\"shunt_resistor\">分流电阻值 (欧姆)</label>";
  html += "<input type=\"number\" id=\"shunt_resistor\" name=\"shunt_resistor\" step=\"0.01\" min=\"0.01\" placeholder=\"例如: 0.1\" value=\"" + String(config.shunt_resistor) + "\">";
  html += "<small style=\"display:block; margin-top:6px; color:var(--text-muted); font-size:12px;\">";
  html += "<strong>配置提示：</strong><br>";
  html += "- 小电流场景 (0-1A)：0.1-0.5Ω<br>";
  html += "- 中等电流场景 (1-5A)：0.05-0.1Ω<br>";
  html += "- 大电流场景 (5A以上)：0.01-0.05Ω<br>";
  html += "请根据实际硬件使用的分流电阻值进行设置。";
  html += "</small>";
  html += "</div>";
  
  // 温湿度报警值设置
  html += "<div class=\"section-title\">温湿度报警设置</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"temp_max\">温度上限 (°C)</label>";
  html += "<input type=\"number\" id=\"temp_max\" name=\"temp_max\" step=\"0.5\" min=\"-20\" max=\"100\" placeholder=\"例如: 30\" value=\"" + String(config.temp_max) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"temp_min\">温度下限 (°C)</label>";
  html += "<input type=\"number\" id=\"temp_min\" name=\"temp_min\" step=\"0.5\" min=\"-20\" max=\"100\" placeholder=\"例如: 0\" value=\"" + String(config.temp_min) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"humidity_max\">湿度上限 (%)</label>";
  html += "<input type=\"number\" id=\"humidity_max\" name=\"humidity_max\" step=\"1\" min=\"0\" max=\"100\" placeholder=\"例如: 80\" value=\"" + String(config.humidity_max) + "\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label for=\"humidity_min\">湿度下限 (%)</label>";
  html += "<input type=\"number\" id=\"humidity_min\" name=\"humidity_min\" step=\"1\" min=\"0\" max=\"100\" placeholder=\"例如: 20\" value=\"" + String(config.humidity_min) + "\">";
  html += "<small style=\"display:block; margin-top:6px; color:var(--text-muted); font-size:12px;\">";
  html += "<strong>配置提示：</strong><br>";
  html += "- 温度范围：-20°C 到 100°C<br>";
  html += "- 湿度范围：0% 到 100%<br>";
  html += "请根据实际环境需求设置报警阈值。";
  html += "</small>";
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
  
  server->send(200, "text/html", html);
}

// 处理保存配置请求
void handleSaveConfig() {
  // 获取表单数据
  String ssid = server->arg("ssid");
  String password = server->arg("password");
  String server_ip = server->arg("server_ip");
  int server_port = server->arg("server_port").toInt();
  String device_id = server->arg("device_id");
  float shunt_resistor = server->arg("shunt_resistor").toFloat();
  float temp_max = server->arg("temp_max").toFloat();
  float temp_min = server->arg("temp_min").toFloat();
  float humidity_max = server->arg("humidity_max").toFloat();
  float humidity_min = server->arg("humidity_min").toFloat();
  
  // 保存到配置
  ssid.toCharArray(config.ssid, sizeof(config.ssid));
  password.toCharArray(config.password, sizeof(config.password));
  server_ip.toCharArray(config.server_ip, sizeof(config.server_ip));
  config.server_port = server_port;
  device_id.toCharArray(config.device_id, sizeof(config.device_id));
  // 确保分流电阻值有效
  if (shunt_resistor > 0) {
    config.shunt_resistor = shunt_resistor;
  } else {
    config.shunt_resistor = 0.1; // 默认值
  }
  // 确保温湿度报警值有效（包含范围检查）
  const float TEMP_MIN_RANGE = -20.0;
  const float TEMP_MAX_RANGE = 100.0;
  const float HUMIDITY_MIN_RANGE = 0.0;
  const float HUMIDITY_MAX_RANGE = 100.0;
  
  bool tempValid = (temp_max > temp_min && 
                    temp_min >= TEMP_MIN_RANGE && 
                    temp_max <= TEMP_MAX_RANGE);
  bool humidityValid = (humidity_max > humidity_min && 
                        humidity_min >= HUMIDITY_MIN_RANGE && 
                        humidity_max <= HUMIDITY_MAX_RANGE);
  
  if (tempValid) {
    config.temp_max = temp_max;
    config.temp_min = temp_min;
  } else {
    config.temp_max = 30.0;
    config.temp_min = 0.0;
    Serial.println("[CFG] 温度报警值无效，使用默认值(0~30°C)");
  }
  if (humidityValid) {
    config.humidity_max = humidity_max;
    config.humidity_min = humidity_min;
  } else {
    config.humidity_max = 80.0;
    config.humidity_min = 20.0;
    Serial.println("[CFG] 湿度报警值无效，使用默认值(20~80%)");
  }
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
  
  server->send(200, "text/html", html);
  
  // 延迟后重启
  delay(3000);
  ESP.restart();
}
/**
 * @brief 处理WiFi扫描请求
 */
void handleScanWiFi() {
  String wifiListHTML = getWiFiListHTML();
  server->send(200, "text/html", wifiListHTML);
}

/**
 * @brief 获取WiFi列表HTML
 * @return WiFi列表的HTML字符串
 */
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