#ifndef __ES8266_MQTT_H
#define __ES8266_MQTT_H

#include "stm32f4xx.h"

/*
 * esp8266_mqtt.h —— OneNET 新版物模型（MQTT）与 ESP8266 AT 指令侧参数集中定义。
 * 与 esp8266_mqtt.c 配合：由模块固件执行 MQTT 连接/订阅/发布，MCU 只发 AT 字符串。
 * 修改产品、设备、鉴权 token 时仅需改本头文件宏，勿在多处硬编码。
 */

//此处是OneNET云服务器的公共实例登陆配置-------------------------------------注意修改为自己的云服务设备信息！！！！
//==================== OneNET MQTT 配置 ====================
// 1. 服务器地址与端口（新版物模型）
#define MQTT_BROKERADDRESS 		"mqtts.heclouds.com"    // OneNET新版物模型域名
#define MQTT_PORT               1883

// 2. 必须修改为你自己OneNET平台的信息！！！
#define MQTT_PRODUCT_ID      "y7l1o7W636"       // 你的产品ID
#define MQTT_DEVICE_NAME     "test"             // 你的设备名
#define MQTT_DEVICE_KEY      "version=2018-10-31&res=products%2Fy7l1o7W636%2Fdevices%2Ftest&et=9999999999&method=md5&sign=cM9C%2BiXlINLMI6r%2BxW9KUg%3D%3D"      // 鉴权信息

// 3. MQTT连接参数（新版物模型协议）
// ClientID格式: 设备名
#define MQTT_CLIENTID        MQTT_DEVICE_NAME
// Username格式: 产品ID
#define MQTT_USARNAME        MQTT_PRODUCT_ID
// Password格式: 鉴权信息（token）
#define MQTT_PASSWD          MQTT_DEVICE_KEY

// ==================== MQTT主题配置（OneNET新版物模型）====================
// 上报属性主题（设备 -> 云端）：发送传感器数据、设备状态
#define MQTT_PUBLISH_TOPIC   "$sys/"MQTT_PRODUCT_ID"/"MQTT_DEVICE_NAME"/thing/property/post"

// 订阅云端下发指令（云端 -> 设备）：接收LED控制等指令
#define MQTT_SUBSCRIBE_TOPIC "$sys/"MQTT_PRODUCT_ID"/"MQTT_DEVICE_NAME"/thing/property/set"

// 订阅上报回执（云端 -> 设备）：接收数据上报的确认结果（可选，用于调试）
#define MQTT_REPLY_TOPIC     "$sys/"MQTT_PRODUCT_ID"/"MQTT_DEVICE_NAME"/thing/property/post/reply"

// 属性设置应答（设备 -> 云端）：平台「属性设置」后必须在超时前发布，否则报 10411
#define MQTT_SET_REPLY_TOPIC "$sys/"MQTT_PRODUCT_ID"/"MQTT_DEVICE_NAME"/thing/property/set_reply"

//此处是阿里云服务器的企业实例登陆配置-------------------------------------注意修改为自己的云服务设备信息！！！！
//#define MQTT_BROKERADDRESS 		"iot-060a065f.mqtt.iothub.aliyuncs.com"
//#define MQTT_CLIENTID 			"0001|securemode=3,signmethod=hmacsha1|"
//#define MQTT_USARNAME 			"smartdevice&g850YXdgU5r"
//#define MQTT_PASSWD 			"A8F93BD31F6085B1AB2AE3CC311E38971B15885D"
//#define	MQTT_PUBLISH_TOPIC 		"/sys/g850YXdgU5r/smartdevice/thing/event/property/post"
//#define MQTT_SUBSCRIBE_TOPIC 	"/sys/g850YXdgU5r/smartdevice/thing/service/property/set"

/* 小端 32 位整数拆字节（若其它模块组 MQTT 负载需要按字节填充可用） */
#define BYTE0(dwTemp)       (*( char *)(&dwTemp))
#define BYTE1(dwTemp)       (*((char *)(&dwTemp) + 1))
#define BYTE2(dwTemp)       (*((char *)(&dwTemp) + 2))
#define BYTE3(dwTemp)       (*((char *)(&dwTemp) + 3))


/**
 * @brief 使用 AT+MQTTUSERCFG 与 AT+MQTTCONN 连接 OneNET MQTT Broker。
 *
 * @param[in] client_id  MQTT 客户端 ID（本工程常为设备名）。
 * @param[in] user_name  MQTT 用户名（本工程常为产品 ID）。
 * @param[in] password   MQTT 密码（鉴权 token）。
 *
 * @retval 0  连接成功。
 * @retval -1 配置或连接在重试后仍失败。
 *
 * @note 完整上电流程请优先使用 esp8266_mqtt_init()。
 */
extern int32_t mqtt_connect(char *client_id,char *user_name,char *password);

/**
 * @brief 订阅或取消订阅指定 MQTT 主题（AT+MQTTSUB / AT+MQTTUNSUB）。
 *
 * @param[in] topic   完整主题字符串。
 * @param[in] qos     服务质量等级，0 或 1。
 * @param[in] whether 1 表示订阅，0 表示取消订阅。
 *
 * @retval 0  操作成功。
 * @retval -1 重试后仍失败。
 */
extern int32_t mqtt_subscribe_topic(char *topic,uint8_t qos,uint8_t whether);

/**
 * @brief 向主题发布 UTF-8/ASCII 负载（内部选择 MQTTPUB 或 MQTTPUBRAW）。
 *
 * @param[in] topic   目标主题。
 * @param[in] message 以 null 结尾的消息体，通常为 JSON。
 * @param[in] qos     QoS 0 或 1。
 *
 * @retval >0 成功时为 strlen(message)。
 * @retval 0  参数非法、长度为 0 或发布失败。
 */
extern uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos);

/**
 * @brief 发送 AT+MQTTPING，维持与 Broker 的 MQTT 会话活跃。
 *
 * @note 是否仍被断开取决于模块 keepalive 与网络；本调用为单次 PING。
 */
extern void mqtt_send_heart(void);

/**
 * @brief ESP8266 串口与网络侧完整初始化：退出透传、AT、WiFi、MQTT 用户、连接与订阅。
 *
 * @retval 0   成功。
 * @retval <0  分阶段错误码，含义见 esp8266_mqtt.c 中 esp8266_mqtt_init 注释。
 */
extern int32_t esp8266_mqtt_init(void);

/**
 * @brief 读取传感器与 GPIO，按 OneNET 物模型格式写入 g_mqtt_msg 并发布。
 *
 * @note 属性标识符须与云端物模型一致；QoS 在实现中固定为 1。
 */
extern void mqtt_report_devices_status(void);

/**
 * @brief 解析 OneNET property/set 下行：控灯并发布 set_reply。
 *
 * @param[in] payload 串口一帧内容（可含 +MQTTSUBRECV 头与 JSON），须以 \\0 结尾。
 *
 * @retval 1 已识别为属性设置并完成应答发布尝试。
 * @retval 0 非属性设置帧，未处理。
 */
extern uint8_t mqtt_handle_property_set(char *payload);

/** 供 esp8266_nettime.c 等模块复用 AT 轮询与接收判稳 */
extern void ESP8266_Clear(void);
extern unsigned char ESP8266_WaitRecive(void);
extern unsigned char ESP8266_SendCmdPolls(char *cmd, char *res, unsigned int polls);

/** USART3 AT 互斥（HTTP 对时 / MQTT 上报 / PING 不可并发） */
extern void esp8266_uart_lock(void);
extern void esp8266_uart_unlock(void);

#endif
