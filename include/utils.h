#ifndef UTILS_H
#define UTILS_H

#include "config.h"

/**
 * @brief 控制蜂鸣器发出声音
 * @param ms 蜂鸣时间（毫秒）
 */
void beep(int ms);

/**
 * @brief 读取电池电压
 * @return 电池电压值（V）
 */
float readBatteryVoltage();

/**
 * @brief 计算电池电量百分比
 * @param voltage 电池电压值
 * @return 电池电量百分比（0-100%）
 */
float calcSOC(float voltage);

/**
 * @brief 从EEPROM读取配置
 */
void readConfig();

/**
 * @brief 保存配置到EEPROM
 */
void saveConfig();

#endif // UTILS_H
