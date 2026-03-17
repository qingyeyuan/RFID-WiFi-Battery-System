#ifndef SENSORS_H
#define SENSORS_H

#include "config.h"
#include <Adafruit_SHT31.h>
#include <Adafruit_INA219.h>
#include <MFRC522.h>

// 电池信息结构
typedef struct {
  String uid;           // RFID UID
  String batteryId;     // 电池编号
  String productionDate; // 生产日期
  int cycleCount;       // 循环充电次数
} BatteryInfo;

// 传感器对象指针（在 initSensors 中创建）
extern Adafruit_SHT31* sht31;
extern Adafruit_INA219* ina219;
extern MFRC522*        mfrc522;

// 全局变量
extern BatteryInfo currentBattery;

/**
 * @brief 初始化传感器
 */
void initSensors();

/**
 * @brief 读取温湿度数据
 * @param temperature 温度变量指针
 * @param humidity 湿度变量指针
 * @return 是否读取成功
 */
bool readTH(float* temperature, float* humidity);

/**
 * @brief 检查RFID卡片
 * @return 是否检测到新卡片
 */
bool checkRFID();

/**
 * @brief 读取INA219电流电压数据
 * @param voltage 电压变量指针
 * @param current 电流变量指针
 * @param power 功率变量指针
 * @return 是否读取成功
 */
bool readINA219(float* voltage, float* current, float* power);

/**
 * @brief 发送数据到服务器
 */
void sendDataToServer();

/**
 * @brief 根据RFID UID获取电池信息
 * @param uid RFID UID
 * @return 电池信息
 */
BatteryInfo getBatteryInfo(const String& uid);

/**
 * @brief 检测电池充电状态并控制LED
 * @return 是否正在充电
 */
bool checkBatteryCharging();

#endif // SENSORS_H