# 服务器数据接口文档

## 1. 接口概述

本设备通过TCP协议向服务器发送传感器数据，采用JSON格式进行数据传输。数据发送间隔为3秒，保持长连接。

## 2. 连接信息

- **连接方式**：TCP协议
- **默认服务器IP**：192.168.1.100
- **默认服务器端口**：8080
- **数据发送间隔**：3秒
- **连接方式**：长连接

## 3. JSON数据格式

设备发送的JSON数据格式如下：

```json
{
  "number": "设备ID",
  "voltage": "电池电压",
  "soc": "电池电量百分比",
  "temp": "温度",
  "humidity": "湿度",
  "rfid": "RFID UID",
  "battery_id": "电池编号",
  "production_date": "生产日期",
  "cycle_count": "循环充电次数",
  "ina_voltage": "INA219电压",
  "ina_current": "INA219电流",
  "ina_power": "INA219功率"
}
```

## 4. 字段说明

| 字段名 | 类型 | 说明 | 示例值 |
|--------|------|------|--------|
| number | 字符串 | 设备ID | "25824" |
| voltage | 字符串 | 电池电压（单位：V），保留2位小数 | "3.05" |
| soc | 字符串 | 电池电量百分比，整数 | "95" |
| temp | 字符串 | 温度（单位：℃），保留1位小数 | "24.5" |
| humidity | 字符串 | 湿度（单位：%），保留1位小数 | "65.2" |
| rfid | 字符串 | RFID卡片UID | "A1B2C3D4" |
| battery_id | 字符串 | 电池编号 | "BAT-001" |
| production_date | 字符串 | 电池生产日期 | "2023-01-01" |
| cycle_count | 字符串 | 电池循环充电次数 | "10" |
| ina_voltage | 字符串 | INA219电压（单位：V），保留2位小数 | "5.12" |
| ina_current | 字符串 | INA219电流（单位：A），保留3位小数 | "0.500" |
| ina_power | 字符串 | INA219功率（单位：W），保留3位小数 | "2.560" |

## 5. 数据来源

| 字段名 | 数据来源 | 传感器 |
|--------|----------|--------|
| number | 设备配置 | 配置文件 |
| voltage | 电池电压 | 内置ADC |
| soc | 电池电量 | 计算得出 |
| temp | 温度 | SHT3X |
| humidity | 湿度 | SHT3X |
| rfid | RFID UID | MFRC522 |
| battery_id | 电池编号 | RFID卡片 |
| production_date | 生产日期 | RFID卡片 |
| cycle_count | 循环充电次数 | RFID卡片 |
| ina_voltage | INA219电压 | INA219 |
| ina_current | INA219电流 | INA219 |
| ina_power | INA219功率 | INA219 |

## 6. 错误处理

- 当WiFi未连接时，设备会跳过数据发送
- 当服务器连接失败时，设备会在下次尝试重新连接
- 当传感器读取失败时，对应字段会发送默认值（0或空字符串）

## 7. 配置方式

设备支持通过Web界面进行配置，包括：
- WiFi网络设置
- 服务器IP和端口设置
- 设备ID设置

## 8. 示例数据

```json
{
  "number": "25824",
  "voltage": "3.05",
  "soc": "95",
  "temp": "24.5",
  "humidity": "65.2",
  "rfid": "A1B2C3D4",
  "battery_id": "BAT-001",
  "production_date": "2023-01-01",
  "cycle_count": "10",
  "ina_voltage": "5.12",
  "ina_current": "0.500",
  "ina_power": "2.560"
}
```

## 9. 服务器端处理建议

1. 接收TCP连接，保持长连接
2. 解析JSON数据，验证字段完整性
3. 存储数据到数据库
4. 发送响应确认收到数据
5. 处理连接断开和重连

## 10. 代码实现参考

### 数据发送核心代码

```cpp
// 构建JSON数据
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
client->print(jsonData);
```

### 连接处理代码

```cpp
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
```