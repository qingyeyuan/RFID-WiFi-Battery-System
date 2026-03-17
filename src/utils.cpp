#include <EEPROM.h>
#include "../include/utils.h"

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
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(BATTERY_PIN);
    delay(2);
  }
  float pinV = (total / 10.0 / ADC_RESOLUTION) * 1.1;
  return pinV * VOLTAGE_DIVIDER_RATIO;
}

/**
 * @brief 计算电池电量百分比
 * @param voltage 电池电压值
 * @return 电池电量百分比（0-100%）
 */
float calcSOC(float voltage) {
  return constrain((voltage - BATT_MIN_V) / (BATT_MAX_V - BATT_MIN_V) * 100.0, 0.0, 100.0);
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
    config.configured = false;
  }
  Serial.println("[CFG] 配置读取完成");
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
