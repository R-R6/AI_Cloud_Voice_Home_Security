#ifndef __ESP8266_H__
#define __ESP8266_H__

/*
 * esp8266.h / esp8266.c —— ESP8266 与 STM32 的 USART3 链路层与通用 AT 命令（透传、TCP 等）。
 * 本工程 OneNET MQTT 主要使用 esp8266_mqtt.c 中的 AT+MQTT* 指令集；
 * WIFI_SSID / WIFI_PASSWORD 由 esp8266_mqtt_init 中 CWJAP 使用。
 */

#define EN_DEBUG_ESP8266	0

//添加WIFI热点宏定义，此处根据自己的wifi作调整
#define WIFI_SSID 			"WZG"
#define WIFI_PASSWORD		"qwertyuiop123"

extern uint8_t  g_esp8266_tx_buf[512];  // 发送缓冲区
extern volatile uint8_t  g_esp8266_rx_buf[512];// 接收缓冲区
extern volatile uint32_t g_esp8266_rx_cnt;// 接收数据计数值

extern volatile uint32_t g_esp8266_transparent_transmission_sta;

/**
 * @brief 经 USART3 发送以 null 结尾的字符串至 ESP8266。
 *
 * @param[in] str 字符串指针。
 */
extern void 	usart3_send_str(char *str);

/**
 * @brief 初始化 USART3（PB10/PB11），波特率由参数指定。
 *
 * @param[in] baud 波特率，如 115200。
 */
extern void 	esp8266_init(uint32_t baud);

/**
 * @brief 发送 AT 并等待 OK，用于模块是否在线的快速自检。
 *
 * @retval 0  正常；-1 超时或失败。
 */
extern int32_t  esp8266_self_test(void);

/**
 * @brief 发送 +++ 序列并延时，退出透传模式。
 *
 * @retval 0 按实现固定返回 0。
 */
extern int32_t 	esp8266_exit_transparent_transmission (void);

/**
 * @brief 配置 CIPMODE 并进入 CIPSEND 提示的透传发送流程。
 *
 * @retval 0 成功；-1/-2 为各阶段超时失败。
 */
extern int32_t 	esp8266_entry_transparent_transmission(void);

/**
 * @brief 使用 AT+CWJAP 系列命令连接指定 AP（分片发送 SSID/密码）。
 *
 * @param[in] ssid 热点名称。
 * @param[in] pswd 热点密码。
 *
 * @retval 0 成功；-1/-2 失败。
 */
extern int32_t 	esp8266_connect_ap(char* ssid,char* pswd);

/**
 * @brief 使用 AT+CIPSTART 建立 TCP 或 UDP 连接。
 *
 * @param[in] mode 协议字符串，如 "TCP"。
 * @param[in] ip   服务器域名或 IP。
 * @param[in] port 远端端口。
 *
 * @retval 0 未命中错误分支；-1 应答不符合预期。
 */
extern int32_t 	esp8266_connect_server(char* mode,char* ip,uint16_t port);

/**
 * @brief 发送 AT+CIPCLOSE 断开 CIP 连接。
 *
 * @retval 0 成功路径；-1 失败。
 */
extern int32_t 	esp8266_disconnect_server(void);

/**
 * @brief 向模块发送 len 字节数据（不修改接收缓冲）。
 *
 * @param[in] buf 数据指针。
 * @param[in] len 字节数。
 */
extern void 	esp8266_send_bytes(uint8_t *buf,uint32_t len);

/**
 * @brief 向模块发送字符串（不修改接收缓冲）。
 *
 * @param[in] buf 以 null 结尾的字符串。
 */
extern void 	esp8266_send_str(char *buf);

/**
 * @brief 清空 g_esp8266_rx_buf 后发送 AT 文本，便于阻塞式等待响应。
 *
 * @param[in] str AT 指令串。
 */
extern void 	esp8266_send_at(char *str);

/**
 * @brief 在接收缓冲中查找子串，配合 delay_ms 轮询直至超时。
 *
 * @param[in] str     子串。
 * @param[in] timeout 超时计数（与 1 ms 延时次数对应）。
 *
 * @retval 0 找到；-1 超时未找到。
 */
extern int32_t  esp8266_find_str_in_rx_packet(char *str,uint32_t timeout);

/**
 * @brief 发送 AT+CIPMUX=b。
 *
 * @param[in] b 0/1 传给 CIPMUX。
 *
 * @retval 0 成功；-1 失败。
 */
extern int32_t  esp8266_enable_multiple_id(uint32_t b);

/**
 * @brief 创建模块 TCP 服务器。
 *
 * @param[in] port 监听端口。
 *
 * @retval 0 成功；-1 失败。
 */
extern int32_t 	esp8266_create_server(uint16_t port);

/**
 * @brief 关闭模块 TCP 服务器。
 *
 * @param[in] port 端口参数（以模块 AT 手册为准）。
 *
 * @retval 0 成功；-1 失败。
 */
extern int32_t 	esp8266_close_server(uint16_t port);

/**
 * @brief 打开或关闭 AT 命令回显（ATE1/ATE0）。
 *
 * @param[in] b 非 0 开回显，0 关回显。
 *
 * @retval 0 成功；-1 失败。
 */
extern int32_t 	esp8266_enable_echo(uint32_t b);

/**
 * @brief 模块硬件复位后轮询 AT 直至响应 OK。
 *
 * @retval 0 复位后通讯恢复；-1 轮询失败。
 */
extern int32_t 	esp8266_reset(void);

#endif




