#ifndef __ESP8266_NETTIME_H__
#define __ESP8266_NETTIME_H__

/*
 * 经 ESP8266 HTTP 从 www.beijing-time.org 取时，写入 STM32 RTC（东八区）。
 * 与 AT+MQTT* 并行：使用 CIPMUX=1 下 link_id=1 的短连接，持 g_mutex_esp8266 访问串口。
 */

/** 后台周期同步间隔（app_task_mqtt）；刷卡解锁不调用，避免 OLED 黑屏等待 HTTP */
#define ESP8266_NETTIME_SYNC_INTERVAL_S  600u

/**
 * @brief 建立 TCP:80，发送 HTTP GET，解析 Date 头并 RTC_SetTime/SetDate（北京时间）。
 *
 * @retval 0  同步成功。
 * @retval -1 串口/连接/解析/写 RTC 任一步失败（RTC 保持原值）。
 */
extern int32_t esp8266_nettime_sync(void);

#endif
