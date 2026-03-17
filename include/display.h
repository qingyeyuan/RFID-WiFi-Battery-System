#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"
#include <Adafruit_SSD1306.h>

// 显示对象指针（在 initDisplay 中创建）
extern Adafruit_SSD1306* display;

/**
 * @brief 初始化OLED显示
 */
void initDisplay();

/**
 * @brief 更新OLED显示
 */
void updateOLED();

/**
 * @brief 打印状态信息到串口和OLED
 */
void printStatus();

#endif // DISPLAY_H