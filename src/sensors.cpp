#include <EEPROM.h>
#include "../include/sensors.h"
#include "../include/utils.h"
#include <WiFi.h>
#include <SPI.h>

// 传感器对象指针
Adafruit_SHT31* sht31 = nullptr;
Adafruit_INA219* ina219 = nullptr;
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
    Serial.println("[ERR] SHT3X 初始化失败，将继续运行但无法读取温湿度数据");
  }

  // 创建 RFID 对象
  SPI.begin(18, 19, 23, 5);
  mfrc522 = new MFRC522(SS_PIN, RST_PIN);
  mfrc522->PCD_Init();
  Serial.println("[OK]  RFID 就绪");

  // I2C2 → INA219
  Wire.begin(I2C2_SDA, I2C2_SCL);
  Wire.setClock(100000); // 设置I2C时钟速度为100kHz

  // 创建 INA219 对象
  ina219 = new Adafruit_INA219();
  
  bool ina219Ok = false;
  for (int i = 0; i < 3 && !ina219Ok; i++) {
    if (ina219->begin(&Wire)) {
      ina219Ok = true;
      Serial.println("[OK]  INA219 就绪");
    } else {
      Serial.printf("[ERR] INA219 初始化失败 (尝试 %d/3)，检查 GPIO21/22 接线!\n", i+1);
      delay(500);
    }
  }
  if (!ina219Ok) {
    Serial.println("[ERR] INA219 初始化失败，将继续运行但无法读取电流电压数据");
  }

  // 创建 TCP 客户端
  client = new WiFiClient();

  // ADC
  analogSetPinAttenuation(BATTERY_PIN, ADC_0db);
  pinMode(BATTERY_PIN, INPUT);
  
  // 充电状态LED
  pinMode(CHARGE_LED_PIN, OUTPUT);
  digitalWrite(CHARGE_LED_PIN, LOW); // 初始关闭
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
 * @brief 读取INA219电流电压数据
 * @param voltage 电压变量指针
 * @param current 电流变量指针
 * @param power 功率变量指针
 * @return 是否读取成功
 */
bool readINA219(float* voltage, float* current, float* power) {
  if (ina219 == nullptr) {
    *voltage = 0.0;
    *current = 0.0;
    *power = 0.0;
    return false;
  }
  
  unsigned long start = millis();
  
  // 尝试读取INA219数据，最多尝试3次
  for (int i = 0; i < 3; i++) {
    *voltage = ina219->getBusVoltage_V();
    *current = ina219->getCurrent_mA() / 1000.0; // 转换为A
    *power = ina219->getPower_mW() / 1000.0;     // 转换为W
    
    if (!isnan(*voltage) && !isnan(*current) && !isnan(*power)) {
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
  *voltage = 0.0;
  *current = 0.0;
  *power = 0.0;
  return false;
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

        lastUpdate = millis();
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
  
  // 尝试读取INA219数据
  float inaVoltage = 0.0;
  float inaCurrent = 0.0;
  float inaPower = 0.0;
  readINA219(&inaVoltage, &inaCurrent, &inaPower);

  // 构建JSON数据（按照要求的命名规范）
  String jsonData = "{";
  jsonData += "\"number\": \"" + String(config.device_id) + "\",";
  jsonData += "\"voltage\": \"" + String(batteryVoltage, 2) + "\",";
  jsonData += "\"soc\": \"" + String(soc, 0) + "\",";
  jsonData += "\"temp\": \"" + String(temperature, 1) + "\",";
  jsonData += "\"humidity\": \"" + String(humidity, 1) + "\",";
  jsonData += "\"rfid\": \"" + lastUID + "\",";
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
 * @brief 从数据块中提取字符串
 * @param buffer 数据缓冲区
 * @param length 缓冲区长度
 * @return 提取的字符串
 */
String extractString(byte* buffer, byte length) {
  String result = "";
  for (byte i = 0; i < length; i++) {
    if (buffer[i] != 0x00 && buffer[i] >= 0x20 && buffer[i] <= 0x7E) { // 只处理可打印字符
      result += (char)buffer[i];
    } else if (buffer[i] == 0x00) {
      break;
    }
  }
  return result;
}

/**
 * @brief 根据RFID UID获取电池信息
 * @param uid RFID UID
 * @return 电池信息
 */
BatteryInfo getBatteryInfo(const String& uid) {
  BatteryInfo info;
  info.uid = uid;
  
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
  
  return info;
}

/**
 * @brief 检测电池充电状态并控制LED
 * @return 是否正在充电
 */
bool checkBatteryCharging() {
  if (ina219 == nullptr) {
    return false;
  }
  
  float voltage = 0.0;
  float current = 0.0;
  float power = 0.0;
  
  // 读取INA219数据
  if (readINA219(&voltage, &current, &power)) {
    // 当电流为负时，表示电池正在充电
    bool isCharging = (current < 0);
    
    // 控制充电状态LED
    digitalWrite(CHARGE_LED_PIN, isCharging ? HIGH : LOW);
    
    return isCharging;
  }
  
  return false;
}