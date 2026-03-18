#ifndef CONFIG_H
#define CONFIG_H

// ================= 引脚定义 =================
// SHT3X 使用 I2C1
#define I2C1_SDA        32
#define I2C1_SCL        33

// INA226 使用 I2C2
#define I2C2_SDA        21
#define I2C2_SCL        22

// RFID
#define SS_PIN          5
#define RST_PIN         27

// 其他
#define BUZZER_PIN      26
#define BOOT_PIN        0  // Boot按钮引脚

// OLED显示
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS  0x3C

// 电池参数（电压范围，用于SOC计算）
const float BATT_MIN_V            = 2.0;
const float BATT_MAX_V            = 3.1;

// EEPROM地址定义
#define EEPROM_SIZE 256
#define EEPROM_CONFIG_ADDR 0
#define EEPROM_MAGIC 0x55AB // 增加魔术数字，以适应新的配置结构

// 配置参数结构体
struct Config {
  uint16_t magic;
  char ssid[32];
  char password[64];
  char server_ip[16];
  int server_port;
  char device_id[32];
  float shunt_resistor; // 分流电阻值（欧姆）
  float temp_max; // 温度上限报警值（°C）
  float temp_min; // 温度下限报警值（°C）
  float humidity_max; // 湿度上限报警值（%）
  float humidity_min; // 湿度下限报警值（%）
  bool configured;
};

// 全局配置变量
extern Config config;

// 状态变量
extern String        lastUID;
extern unsigned long lastSend;
extern unsigned long bootPressTime;
const unsigned long SEND_INTERVAL = 3000;  // 发送间隔（3秒）
const unsigned long BOOT_PRESS_THRESHOLD = 3000;  // Boot按钮长按阈值（3秒）

#endif // CONFIG_H
