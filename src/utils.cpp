#include <EEPROM.h>
#include "../include/utils.h"
#include "../include/sensors.h"

/**
 * @brief 控制蜂鸣器发出声音
 * @param ms 蜂鸣时间（毫秒）
 */
void beep(int ms) {
  digitalWrite(BUZZER_PIN, LOW);  // 低电平触发
  delay(ms);
  digitalWrite(BUZZER_PIN, HIGH); // 高电平停止
}

/**
 * @brief 读取电池电压
 * @return 电池电压值（V）
 */
float readBatteryVoltage() {
  // 使用INA226测量电池电压
  if (ina226 != nullptr) {
    float voltage = ina226->getBusVoltage_V();
    if (!isnan(voltage) && voltage > 0) {
      return voltage;
    }
  }
  return 0.0;
}

/**
 * @brief 计算电池电量百分比
 * @param voltage 电池电压值
 * @return 电池电量百分比（0-100%）
 */
float calcSOC(float voltage) {
  // 检查电压是否有效
  if (voltage <= 0) {
    Serial.println("[ERR] 电压值无效，返回电量0%");
    return 0.0;
  }
  
  float soc = constrain((voltage - BATT_MIN_V) / (BATT_MAX_V - BATT_MIN_V) * 100.0, 0.0, 100.0);
  Serial.printf("[DBG] 电压: %.2fV, 电量: %.0f%%\n", voltage, soc);
  return soc;
}

/**
 * @brief 从EEPROM读取配置
 */
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
    config.shunt_resistor = 0.1; // 默认分流电阻值为0.1欧姆
    config.temp_max = 30.0; // 默认温度上限报警值为30°C
    config.temp_min = 0.0; // 默认温度下限报警值为0°C
    config.humidity_max = 80.0; // 默认湿度上限报警值为80%
    config.humidity_min = 20.0; // 默认湿度下限报警值为20%
    config.configured = false;
  } else {
    // 确保分流电阻值有效
    if (config.shunt_resistor <= 0 || isnan(config.shunt_resistor)) {
      config.shunt_resistor = 0.1; // 如果分流电阻值无效，使用默认值
      Serial.println("[CFG] 分流电阻值无效，使用默认值0.1欧姆");
    }
    // 确保温湿度报警值有效（包含范围检查）
    const float TEMP_MIN_RANGE = -20.0;
    const float TEMP_MAX_RANGE = 100.0;
    const float HUMIDITY_MIN_RANGE = 0.0;
    const float HUMIDITY_MAX_RANGE = 100.0;
    
    bool tempValid = (config.temp_max > config.temp_min && 
                      config.temp_min >= TEMP_MIN_RANGE && 
                      config.temp_max <= TEMP_MAX_RANGE &&
                      !isnan(config.temp_max) && !isnan(config.temp_min));
    bool humidityValid = (config.humidity_max > config.humidity_min && 
                          config.humidity_min >= HUMIDITY_MIN_RANGE && 
                          config.humidity_max <= HUMIDITY_MAX_RANGE &&
                          !isnan(config.humidity_max) && !isnan(config.humidity_min));
    
    if (!tempValid) {
      config.temp_max = 30.0;
      config.temp_min = 0.0;
      Serial.println("[CFG] 温度报警值无效，使用默认值(0~30°C)");
    }
    if (!humidityValid) {
      config.humidity_max = 80.0;
      config.humidity_min = 20.0;
      Serial.println("[CFG] 湿度报警值无效，使用默认值(20~80%)");
    }
  }
  Serial.println("[CFG] 配置读取完成");
  Serial.printf("[CFG] 分流电阻值: %.2f欧姆\n", config.shunt_resistor);
  Serial.printf("[CFG] 温度报警范围: %.1f°C - %.1f°C\n", config.temp_min, config.temp_max);
  Serial.printf("[CFG] 湿度报警范围: %.0f%% - %.0f%%\n", config.humidity_min, config.humidity_max);
}

/**
 * @brief 保存配置到EEPROM
 */
void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_CONFIG_ADDR, config);
  EEPROM.commit();
  Serial.println("[CFG] 配置已保存");
}
