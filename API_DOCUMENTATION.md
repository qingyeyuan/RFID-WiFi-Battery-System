# TCP数据接口

> ESP32设备 → TCP服务器，JSON格式，长连接

## 连接参数

| 参数 | 默认值 | 配置方式 |
|------|--------|----------|
| 协议 | TCP | - |
| 服务器IP | 192.168.1.100 | Web配网页面 |
| 端口 | 8080 | Web配网页面 |
| 发送间隔 | 3秒 | 代码常量 |
| 连接模式 | 长连接 | - |

## JSON数据格式

```json
{
  "number": "设备ID",
  "soc": "电量百分比",
  "temp": "温度",
  "humidity": "湿度",
  "battery_id": "电池编号",
  "production_date": "生产日期",
  "cycle_count": "循环次数",
  "ina_voltage": "电压",
  "ina_current": "电流",
  "ina_power": "功率"
}
```

## 字段说明

| 字段 | 类型 | 精度 | 来源 | 说明 |
|------|------|------|------|------|
| number | string | - | 配置 | 设备ID |
| soc | string | 整数 | 计算 | 电量(%): 基于电压线性计算 |
| temp | string | 1位小数 | SHT3X | 温度(℃) |
| humidity | string | 1位小数 | SHT3X | 湿度(%) |
| battery_id | string | - | RFID块4 | 电池编号 |
| production_date | string | - | RFID块5 | 生产日期 |
| cycle_count | string | 整数 | RFID块6 | 循环次数（刷卡自动+1） |
| ina_voltage | string | 2位小数 | INA226 | 电压(V) |
| ina_current | string | 3位小数 | INA226 | 电流(A) |
| ina_power | string | 3位小数 | INA226 | 功率(W) |

## 示例数据

```json
{
  "number": "25824",
  "soc": "95",
  "temp": "24.5",
  "humidity": "65.2",
  "battery_id": "BAT-001",
  "production_date": "2023-01-01",
  "cycle_count": "10",
  "ina_voltage": "5.12",
  "ina_current": "0.500",
  "ina_power": "2.560"
}
```

## 错误处理

| 场景 | 设备行为 |
|------|----------|
| WiFi未连接 | 跳过发送，下次重试 |
| TCP连接失败 | 5秒超时，下次重试 |
| 传感器失败 | 字段发送默认值(0或空) |
| RFID未刷卡 | battery_id="UNKNOWN", cycle_count=0 |

## 服务器端建议

1. 保持长连接，避免频繁建立连接
2. 解析JSON，验证必填字段
3. 响应任意内容确认收到
4. 处理断线重连

## 配置参数

设备通过Web配网页面配置（热点: Device-Config, 密码: 12345678）：

| 参数 | 说明 | 默认值 |
|------|------|--------|
| WiFi SSID/密码 | 网络连接 | - |
| 服务器IP | TCP服务器地址 | 192.168.1.100 |
| 服务器端口 | TCP端口 | 8080 |
| 设备ID | 唯一标识 | 25824 |
| 分流电阻值 | INA226精度 | 0.1Ω |
| 温度上限/下限 | 报警阈值 | 30°C / 0°C |
| 湿度上限/下限 | 报警阈值 | 80% / 20% |

---

**实现代码**: `src/sensors.cpp` → `sendDataToServer()`
