#include <EEPROM.h>
#include "include/config.h"
#include "include/utils.h"
#include "include/sensors.h"
#include "include/network.h"
#include "include/display.h"
#define ESP32_DISABLE_BROWNOUT_DETECTOR 1

// 全局变量定义
Config config;
String        lastUID    = "Waiting...";
unsigned long lastUpdate = 0;
unsigned long lastSend   = 0;
unsigned long bootPressTime = 0;

// ================= 初始化 =================
void setup() {
  Serial.begin(115200);
  delay(1000);  // 等待串口稳定
  Serial.println("\n\n========== 系统启动 ==========");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);  // 初始化为高电平（不响）
  
  // 初始化充电状态LED
  pinMode(CHARGE_LED_PIN, OUTPUT);
  digitalWrite(CHARGE_LED_PIN, LOW);  // 初始化为低电平（关闭）
  
  // 初始化Boot按钮
  pinMode(BOOT_PIN, INPUT);

  // 初始化EEPROM
  EEPROM.begin(EEPROM_SIZE);
  readConfig();

  // 初始化传感器
  initSensors();

  // 初始化显示
  initDisplay();

  // 初始化网络
  initNetwork();

  Serial.println("========== 系统就绪 ==========");
}

// ================= 主循环 =================
void loop() {
  // RFID 轮询
  if (checkRFID()) {
    printStatus();
  }

  // 定时刷新（2秒）
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate >= 2000) {
    lastStatusUpdate = millis();
    printStatus();
    
    // 检查电池充电状态并控制LED
    bool isCharging = checkBatteryCharging();
    if (isCharging) {
      digitalWrite(CHARGE_LED_PIN, HIGH);
      Serial.println("[BATT] 电池正在充电...");
    } else {
      digitalWrite(CHARGE_LED_PIN, LOW);
    }
    
    // 检查温度和湿度，超过阈值触发警报
    float t, h;
    if (readTH(&t, &h)) {
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

  // 网络请求处理
  handleNetwork();
  
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
