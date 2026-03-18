#include <EEPROM.h>
#include "../include/display.h"
#include "../include/utils.h"
#include "../include/sensors.h"
#include <WiFi.h>

// 显示对象指针
Adafruit_SSD1306* display = nullptr;

/**
 * @brief 初始化OLED显示
 */
void initDisplay() {
  // 创建 OLED 对象
  display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
  
  bool displayOk = false;
  for (int i = 0; i < 3 && !displayOk; i++) {
    if(display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      displayOk = true;
      Serial.println("[OK]  OLED 就绪");
      display->clearDisplay();
      display->setTextSize(1);
      display->setTextColor(SSD1306_WHITE);
      display->setCursor(0, 0);
      display->println("Starting...");
      display->display();
    } else {
      Serial.printf("[ERR] OLED 初始化失败 (尝试 %d/3)，检查 GPIO32/33 接线!\n", i+1);
      delay(500);
    }
  }
  if (!displayOk) {
    delete display;
    display = nullptr;
    Serial.println("[ERR] OLED 初始化失败，将继续运行但无法显示数据");
  }
}

/**
 * @brief 更新OLED显示
 */
void updateOLED() {
  if (display == nullptr) return;
  
  unsigned long start = millis();
  
  try {
    // 读取传感器数据
    float t = 0.0;
    float h = 0.0;
    readTH(&t, &h);

    float batteryVoltage = readBatteryVoltage();
    float soc  = calcSOC(batteryVoltage);
    
    // 读取INA226数据
    float inaVoltage = 0.0;
    float inaCurrent = 0.0;
    float inaPower = 0.0;
    readINA226(&inaVoltage, &inaCurrent, &inaPower);
    
    // 清除显示
    display->clearDisplay();
    
    // 设置字体大小和颜色
    display->setTextSize(1);
    display->setTextColor(SSD1306_WHITE);
    
    // 第一行：温度和湿度
    display->setCursor(0, 0);
    display->print("T:");
    display->print(t, 1);
    display->print("C");
    display->setCursor(70, 0);
    display->print("H:");
    display->print(h, 1);
    display->print("%");
    
    // 第二行：INA226电压和电量
    display->setCursor(0, 10);
    display->print("V:");
    display->print(inaVoltage, 2);
    display->print("V");
    display->setCursor(70, 10);
    display->print("SOC:");
    display->print(soc, 0);
    display->print("%");

    // 第三行：电流和功率
    display->setCursor(0, 20);
    display->print("I:");
    display->print(inaCurrent, 3);
    display->print("A");
    display->setCursor(70, 20);
    display->print("P:");
    display->print(inaPower, 2);
    display->print("W");

    // 第四行：WiFi状态和循环次数
    display->setCursor(0, 30);
    display->print("WiFi:");
    display->print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
    display->setCursor(70, 30);
    display->print("Cycle:");
    display->print(currentBattery.cycleCount);

    // 第五行：电池编号
    display->setCursor(0, 40);
    display->print("ID:");
    display->print(currentBattery.batteryId.length() > 10 ? currentBattery.batteryId.substring(0, 10) : currentBattery.batteryId);
    
    // 第六行：生产日期
    display->setCursor(0, 50);
    display->print("Date:");
    display->print(currentBattery.productionDate.length() > 10 ? currentBattery.productionDate.substring(0, 10) : currentBattery.productionDate);
    
  
    // 刷新显示
    display->display();
  } catch (...) {
    // 忽略错误，避免程序卡住
    Serial.println("[ERR] 显示更新错误");
  }
  
  // 确保操作不超过500ms
  if (millis() - start > 500) {
    Serial.println("[ERR] 显示更新超时");
  }
}

/**
 * @brief 打印状态信息到串口和OLED
 */
void printStatus() {
  if (sht31 == nullptr) return;
  
  float t = sht31->readTemperature();
  float h = sht31->readHumidity();
  if (isnan(t)) t = 0.0;
  if (isnan(h)) h = 0.0;

  float batteryVoltage = readBatteryVoltage();
  float soc  = calcSOC(batteryVoltage);
  
  // 读取INA226数据
  float inaVoltage = 0.0;
  float inaCurrent = 0.0;
  float inaPower = 0.0;
  readINA226(&inaVoltage, &inaCurrent, &inaPower);

  Serial.println("─────────────────────────");
  Serial.printf("温度: %.1f°C   湿度: %.1f%%\n", t, h);
  Serial.printf("电压: %.2fV   电量: %.0f%%\n", inaVoltage, soc);
  Serial.printf("电流: %.3fA   功率: %.2fW\n", inaCurrent, inaPower);
  Serial.printf("WiFi: %s   循环次数: %d\n", WiFi.status() == WL_CONNECTED ? "ON" : "OFF", currentBattery.cycleCount);
  Serial.printf("电池编号: %s\n", currentBattery.batteryId.c_str());
  Serial.printf("生产日期: %s\n", currentBattery.productionDate.c_str());
  
  // 更新OLED显示
  updateOLED();
}