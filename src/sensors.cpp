#include <EEPROM.h>
#include "../include/sensors.h"
#include "../include/utils.h"
#include <WiFi.h>
#include <SPI.h>

// 传感器对象指针
Adafruit_SHT31* sht31 = nullptr;
INA226_WE*        ina226 = nullptr;
MFRC522*        mfrc522 = nullptr;
WiFiClient*     client = nullptr;

// 全局变量
BatteryInfo currentBattery;

/**
 * @brief 初始化传感器
 */
void initSensors() {
  // I2C1 → SHT3X（仅需要这一条总线）
  Wire1.begin(I2C1_SDA, I2C1_SCL);
  Wire1.setClock(100000); // 设置I2C时钟速度为100kHz

  // 创建 SHT3X 对象
  sht31 = new Adafruit_SHT31(&Wire1);
  
  bool sht31Ok = false;
  for (int i = 0; i < 3 && !sht31Ok; i++) {
    if (sht31->begin(0x44) || sht31->begin(0x45)) {
      sht31Ok = true;
      Serial.println("[OK]  SHT3X 就绪");
    } else {
      Serial.printf("[ERR] SHT3X 初始化失败 (尝试 %d/3)，检查 GPIO32/33 接线!\n", i+1);
      delay(500);
    }
  }
  if (!sht31Ok) {
    delete sht31;
    sht31 = nullptr;
    Serial.println("[ERR] SHT3X 初始化失败，将继续运行但无法读取温湿度数据");
  }

  // 创建 RFID 对象
  SPI.begin(18, 19, 23, 5);
  mfrc522 = new MFRC522(SS_PIN, RST_PIN);
  mfrc522->PCD_Init();
  Serial.println("[OK]  RFID 就绪");

  // I2C2 → INA226
  Wire.begin(I2C2_SDA, I2C2_SCL);
  Wire.setClock(100000); // 设置I2C时钟速度为100kHz

  // 创建 INA226 对象
  ina226 = new INA226_WE(&Wire);
  
  bool ina226Ok = false;
  for (int i = 0; i < 3 && !ina226Ok; i++) {
    if (ina226->init()) {
      ina226Ok = true;
      
      // 设置分流电阻
      float resistorValue = 0.1;  // 默认值
      bool useDefault = false;
      
      // 检查配置值是否有效
      if (config.shunt_resistor > 0 && !isnan(config.shunt_resistor)) {
        // 尝试使用配置值
        ina226->setResistorRange(config.shunt_resistor);
        if (ina226->getI2cErrorCode() == 0) {
          resistorValue = config.shunt_resistor;
          Serial.printf("[OK]  分流电阻值设置成功: %.2f欧姆\n", resistorValue);
        } else {
          // 配置值设置失败，使用默认值重试
          useDefault = true;
          Serial.printf("[WARN] 配置值 %.2f欧姆 设置失败，尝试默认值...\n", config.shunt_resistor);
        }
      } else {
        useDefault = true;
        Serial.printf("[WARN] 分流电阻配置值无效(%.2f)，使用默认值\n", config.shunt_resistor);
      }
      
      // 使用默认值
      if (useDefault) {
        ina226->setResistorRange(0.1);
        if (ina226->getI2cErrorCode() == 0) {
          Serial.printf("[OK]  使用默认分流电阻值: 0.1欧姆\n");
        } else {
          Serial.println("[ERR] 分流电阻设置失败，电流测量可能不准确");
        }
      }
      
      Serial.println("[OK]  INA226 就绪");
    } else {
      Serial.printf("[ERR] INA226 初始化失败 (尝试 %d/3)，检查 GPIO21/22 接线!\n", i+1);
      delay(500);
    }
  }
  if (!ina226Ok) {
    delete ina226;
    ina226 = nullptr;
    Serial.println("[ERR] INA226 初始化失败，将继续运行但无法读取电流电压数据");
  }

  // 创建 TCP 客户端
  client = new WiFiClient();
}

/**
 * @brief 读取温湿度数据
 * @param temperature 温度变量指针
 * @param humidity 湿度变量指针
 * @return 是否读取成功
 */
bool readTH(float* temperature, float* humidity) {
  if (sht31 == nullptr) {
    *temperature = 0.0;
    *humidity = 0.0;
    return false;
  }
  
  unsigned long start = millis();
  
  // 尝试读取温湿度数据，最多尝试3次
  for (int i = 0; i < 3; i++) {
    *temperature = sht31->readTemperature();
    *humidity = sht31->readHumidity();
    
    if (!isnan(*temperature) && !isnan(*humidity)) {
      // 检查温湿度是否超出报警阈值
      if (*temperature > config.temp_max) {
        Serial.printf("[ALARM] 温度过高: %.1f°C (上限: %.1f°C)\n", *temperature, config.temp_max);
        beep(500); // 长鸣报警
      } else if (*temperature < config.temp_min) {
        Serial.printf("[ALARM] 温度过低: %.1f°C (下限: %.1f°C)\n", *temperature, config.temp_min);
        beep(500); // 长鸣报警
      }
      
      if (*humidity > config.humidity_max) {
        Serial.printf("[ALARM] 湿度过高: %.1f%% (上限: %.1f%%)\n", *humidity, config.humidity_max);
        beep(500); // 长鸣报警
      } else if (*humidity < config.humidity_min) {
        Serial.printf("[ALARM] 湿度过低: %.1f%% (下限: %.1f%%)\n", *humidity, config.humidity_min);
        beep(500); // 长鸣报警
      }
      
      return true;
    }
    
    // 每次失败后延迟100ms
    delay(100);
    
    // 超时检查
    if (millis() - start > 1000) {
      break;
    }
  }
  
  // 读取失败，设置默认值
  *temperature = 0.0;
  *humidity = 0.0;
  return false;
}

/**
 * @brief 读取INA226电流电压数据
 * @param voltage 电压变量指针
 * @param current 电流变量指针
 * @param power 功率变量指针
 * @return 是否读取成功
 */
bool readINA226(float* voltage, float* current, float* power) {
  if (ina226 == nullptr) {
    *voltage = 0.0;
    *current = 0.0;
    *power = 0.0;
    Serial.println("[ERR] INA226 对象为nullptr");
    return false;
  }
  
  unsigned long start = millis();
  bool voltageValid = false;
  
  // 尝试读取INA226数据，最多尝试3次
  for (int i = 0; i < 3; i++) {
    *voltage = ina226->getBusVoltage_V(); // 直接获取V
    *current = ina226->getCurrent_A();    // 直接获取A
    *power = ina226->getBusPower();       // 获取W
    
    Serial.printf("[DBG] 读取INA226数据 (尝试 %d/3): 电压=%.2fV, 电流=%.3fA, 功率=%.3fW\n", i+1, *voltage, *current, *power);
    
    // 检查电压是否有效
    if (!isnan(*voltage) && *voltage > 0) {
      voltageValid = true;
    }
    
    // 检查所有数据是否有效
    if (voltageValid && !isnan(*current) && !isnan(*power)) {
      Serial.println("[OK] INA226数据读取成功");
      return true;
    }
    
    // 每次失败后延迟100ms
    delay(100);
    
    // 超时检查
    if (millis() - start > 1000) {
      break;
    }
  }
  
  // 再次检查电压是否有效，确保即使循环中没有设置voltageValid，也能正确处理
  if (!isnan(*voltage) && *voltage > 0) {
    voltageValid = true;
  }
  
  // 如果电压有效但电流或功率无效，保持电压值
  if (voltageValid) {
    *current = 0.0;
    *power = 0.0;
    Serial.println("[WARN] INA226电流或功率读取失败，只返回电压值");
    return true; // 返回true，因为至少电压是有效的
  } else {
    // 所有数据都无效，设置默认值
    *voltage = 0.0;
    *current = 0.0;
    *power = 0.0;
    Serial.println("[ERR] INA226数据读取失败");
    return false;
  }
}

/**
 * @brief 检查RFID卡片
 * @return 是否检测到新卡片
 */
bool checkRFID() {
  if (mfrc522 == nullptr) return false;
  
  unsigned long start = millis();
  
  try {
    // 检查是否有新卡片
    if (millis() - start > 500) { // 500ms超时
      return false;
    }
    
    if (mfrc522->PICC_IsNewCardPresent()) {
      if (millis() - start > 500) { // 500ms超时
        return false;
      }
      
      if (mfrc522->PICC_ReadCardSerial()) {
        lastUID = "";
        for (byte i = 0; i < mfrc522->uid.size; i++) {
          if (mfrc522->uid.uidByte[i] < 0x10) lastUID += '0';
          lastUID += String(mfrc522->uid.uidByte[i], HEX);
        }
        lastUID.toUpperCase();

        // 打印卡片类型信息
        MFRC522::PICC_Type piccType = mfrc522->PICC_GetType(mfrc522->uid.sak);
        Serial.printf("[DBG] 卡片类型: %s\n", (const char*)mfrc522->PICC_GetTypeName(piccType));
        Serial.printf("[DBG] SAK: 0x%02X\n", mfrc522->uid.sak);

        // 获取电池信息（在停止卡片前读取块数据）
        currentBattery = getBatteryInfo(lastUID);
        
        // 增加循环次数
        currentBattery.cycleCount++;
        Serial.println(">>> 循环次数已更新: " + String(currentBattery.cycleCount));
        
        // 将更新后的循环次数写回卡片
        byte block6Buffer[16] = {0}; // 16字节缓冲区，初始化为0
        String cycleCountStr = String(currentBattery.cycleCount);
        cycleCountStr.getBytes(block6Buffer, 16); // 将字符串转换为字节数组
        
        if (writeBlock(6, block6Buffer)) {
          Serial.println(">>> 循环次数已写入卡片");
        } else {
          Serial.println(">>> 循环次数写入失败");
        }
        
        // 尝试停止卡片，即使失败也继续
        try {
          mfrc522->PICC_HaltA();
          mfrc522->PCD_StopCrypto1();
        } catch (...) {
          // 忽略错误
        }

        // 打印电池信息到串口
        Serial.println(">>> 刷卡 UID: " + lastUID);
        Serial.println(">>> 电池编号: " + currentBattery.batteryId);
        Serial.println(">>> 生产日期: " + currentBattery.productionDate);
        Serial.println(">>> 循环充电次数: " + String(currentBattery.cycleCount));
        beep(80);

        return true;
      }
    }
  } catch (...) {
    // 忽略错误，避免程序卡住
    Serial.println("[ERR] RFID 读取错误");
  }
  
  return false;
}

/**
 * @brief 发送数据到服务器
 */
void sendDataToServer() {
  // 检查WiFi连接状态
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TCP] WiFi未连接，跳过数据发送");
    return;
  }
  
  if (client == nullptr) return;

  // 检查TCP连接状态，如果未连接则尝试连接
  if (!client->connected()) {
    Serial.print("[TCP] 连接服务器 " + String(config.server_ip) + ":" + String(config.server_port) + "...");
    
    // 重置客户端，确保干净的连接
    client->stop();
    
    // 设置连接超时为5秒
    unsigned long connectStart = millis();
    bool connected = false;
    
    while (millis() - connectStart < 5000 && !connected) {
      if (client->connect(config.server_ip, config.server_port)) {
        connected = true;
        Serial.println("成功");
      } else {
        Serial.print(".");
        delay(300);
      }
    }
    
    if (!connected) {
      Serial.println("失败（超时）");
      Serial.println("[TCP] 请检查服务器IP、端口和网络连接");
      return;
    }
  }

  // 读取传感器数据
  float temperature = 0.0;
  float humidity = 0.0;
  float batteryVoltage = readBatteryVoltage();
  float soc = calcSOC(batteryVoltage);
  
  // 尝试读取温湿度数据
  readTH(&temperature, &humidity);
  
  // 尝试读取INA226数据
  float inaVoltage = 0.0;
  float inaCurrent = 0.0;
  float inaPower = 0.0;
  readINA226(&inaVoltage, &inaCurrent, &inaPower);

  // 构建JSON数据（按照要求的命名规范）
  String jsonData = "{";
  jsonData += "\"number\": \"" + String(config.device_id) + "\",";
  jsonData += "\"soc\": \"" + String(soc, 0) + "\",";
  jsonData += "\"temp\": \"" + String(temperature, 1) + "\",";
  jsonData += "\"humidity\": \"" + String(humidity, 1) + "\",";
  jsonData += "\"battery_id\": \"" + currentBattery.batteryId + "\",";
  jsonData += "\"production_date\": \"" + currentBattery.productionDate + "\",";
  jsonData += "\"cycle_count\": \"" + String(currentBattery.cycleCount) + "\",";
  jsonData += "\"ina_voltage\": \"" + String(inaVoltage, 2) + "\",";
  jsonData += "\"ina_current\": \"" + String(inaCurrent, 3) + "\",";
  jsonData += "\"ina_power\": \"" + String(inaPower, 3) + "\"";
  jsonData += "}";

  // 发送JSON数据
  try {
    client->print(jsonData);
    Serial.println("[TCP] 数据已发送: " + jsonData);
    
    // 等待服务器响应
    unsigned long start = millis();
    while (millis() - start < 2000 && !client->available()) {
      delay(10);
    }
    
    // 读取服务器响应
    if (client->available()) {
      String response = client->readStringUntil('\n');
      Serial.println("[TCP] 服务器响应: " + response);
    } else {
      Serial.println("[TCP] 未收到服务器响应");
    }
  } catch (...) {
    Serial.println("[TCP] 数据发送错误");
    // 发生错误时关闭连接，下次会重新连接
    client->stop();
  }
  
  // 保持长连接，不关闭连接
  Serial.println("[TCP] 保持长连接");
}

/**
 * @brief 从RFID卡片读取数据块
 * @param blockAddr 块地址
 * @param buffer 数据缓冲区
 * @return 是否读取成功
 */
bool readBlock(byte blockAddr, byte* buffer) {
  if (mfrc522 == nullptr) return false;
  
  // MIFARE卡片每个块大小为16字节
  byte bufferSize = 18; // 确保缓冲区足够大
  
  // 默认密钥（用于MIFARE Classic卡片）
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF; // 默认密钥
  }
  
  // 授权块
  MFRC522::StatusCode status = mfrc522->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &mfrc522->uid);
  if (status != MFRC522::STATUS_OK) {
    Serial.printf("[ERR] 授权失败: %s\n", mfrc522->GetStatusCodeName(status));
    return false;
  }
  
  // 读取块数据
  status = mfrc522->MIFARE_Read(blockAddr, buffer, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    Serial.printf("[ERR] 读取失败: %s\n", mfrc522->GetStatusCodeName(status));
    return false;
  }
  
  // 打印读取的数据（调试用）
  Serial.printf("[DBG] 块 %d 数据: ", blockAddr);
  for (byte i = 0; i < 16; i++) { // 只打印16字节数据
    Serial.printf("%02X ", buffer[i]);
  }
  Serial.println();
  
  return true;
}

/**
 * @brief 向RFID卡片写入数据块
 * @param blockAddr 块地址
 * @param buffer 数据缓冲区
 * @return 是否写入成功
 */
bool writeBlock(byte blockAddr, byte* buffer) {
  if (mfrc522 == nullptr) return false;
  
  // 默认密钥（用于MIFARE Classic卡片）
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF; // 默认密钥
  }
  
  // 授权块
  MFRC522::StatusCode status = mfrc522->PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &mfrc522->uid);
  if (status != MFRC522::STATUS_OK) {
    Serial.printf("[ERR] 授权失败: %s\n", mfrc522->GetStatusCodeName(status));
    return false;
  }
  
  // 写入块数据
  status = mfrc522->MIFARE_Write(blockAddr, buffer, 16); // 写入16字节
  if (status != MFRC522::STATUS_OK) {
    Serial.printf("[ERR] 写入失败: %s\n", mfrc522->GetStatusCodeName(status));
    return false;
  }
  
  // 打印写入的数据（调试用）
  Serial.printf("[DBG] 块 %d 已写入: ", blockAddr);
  for (byte i = 0; i < 16; i++) { // 只打印16字节数据
    Serial.printf("%02X ", buffer[i]);
  }
  Serial.println();
  
  return true;
}

/**
 * @brief 根据RFID UID获取电池信息
 * @return 电池信息
 */
BatteryInfo getBatteryInfo(const String& uid) {
  BatteryInfo info;
  
  // 初始化默认值
  info.batteryId = "UNKNOWN";
  info.productionDate = "N/A";
  info.cycleCount = 0;
  
  // 读取块4获取电池编号
  byte block4Buffer[18]; // 增大缓冲区
  if (readBlock(4, block4Buffer)) {
    // 直接处理缓冲区，避免使用String
    char batteryId[17]; // 16个字符 + 结束符
    int j = 0;
    for (int i = 0; i < 16 && j < 16; i++) {
      if (block4Buffer[i] != 0x00 && block4Buffer[i] >= 0x20 && block4Buffer[i] <= 0x7E) {
        batteryId[j++] = (char)block4Buffer[i];
      } else if (block4Buffer[i] == 0x00) {
        break;
      }
    }
    batteryId[j] = '\0';
    info.batteryId = String(batteryId);
  }
  
  // 读取块5获取生产日期
  byte block5Buffer[18]; // 增大缓冲区
  if (readBlock(5, block5Buffer)) {
    // 直接处理缓冲区，避免使用String
    char productionDate[17]; // 16个字符 + 结束符
    int j = 0;
    for (int i = 0; i < 16 && j < 16; i++) {
      if (block5Buffer[i] != 0x00 && block5Buffer[i] >= 0x20 && block5Buffer[i] <= 0x7E) {
        productionDate[j++] = (char)block5Buffer[i];
      } else if (block5Buffer[i] == 0x00) {
        break;
      }
    }
    productionDate[j] = '\0';
    info.productionDate = String(productionDate);
  }
  
  // 读取块6获取循环次数
  byte block6Buffer[18]; // 增大缓冲区
  if (readBlock(6, block6Buffer)) {
    // 尝试从缓冲区中解析循环次数
    char cycleCountStr[17]; // 16个字符 + 结束符
    int j = 0;
    for (int i = 0; i < 16 && j < 16; i++) {
      if (block6Buffer[i] != 0x00 && block6Buffer[i] >= 0x20 && block6Buffer[i] <= 0x7E) {
        cycleCountStr[j++] = (char)block6Buffer[i];
      } else if (block6Buffer[i] == 0x00) {
        break;
      }
    }
    cycleCountStr[j] = '\0';
    // 将字符串转换为整数
    info.cycleCount = atoi(cycleCountStr);
  }
  
  return info;
}