#ifndef NETWORK_H
#define NETWORK_H

#include "config.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClient.h>

// 网络对象指针（在 initNetwork 中创建）
extern WebServer* server;
extern DNSServer* dnsServer;
extern WiFiClient* client;

/**
 * @brief 初始化网络
 */
void initNetwork();

/**
 * @brief 启动配网模式
 */
void startConfigMode();

/**
 * @brief 处理网络请求
 */
void handleNetwork();

/**
 * @brief 处理根路径请求
 */
void handleRoot();

/**
 * @brief 处理保存配置请求
 */
void handleSaveConfig();

/**
 * @brief 处理WiFi扫描请求
 */
void handleScanWiFi();

/**
 * @brief 获取WiFi列表HTML
 * @return WiFi列表的HTML字符串
 */
String getWiFiListHTML();

#endif // NETWORK_H