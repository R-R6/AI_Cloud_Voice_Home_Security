#ifndef __ES8266_MQTT_H
#define __ES8266_MQTT_H

#include "stm32f4xx.h"



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

//此处是阿里云服务器的企业实例登陆配置-------------------------------------注意修改为自己的云服务设备信息！！！！
//#define MQTT_BROKERADDRESS 		"iot-060a065f.mqtt.iothub.aliyuncs.com"
//#define MQTT_CLIENTID 			"0001|securemode=3,signmethod=hmacsha1|"
//#define MQTT_USARNAME 			"smartdevice&g850YXdgU5r"
//#define MQTT_PASSWD 			"A8F93BD31F6085B1AB2AE3CC311E38971B15885D"
//#define	MQTT_PUBLISH_TOPIC 		"/sys/g850YXdgU5r/smartdevice/thing/event/property/post"
//#define MQTT_SUBSCRIBE_TOPIC 	"/sys/g850YXdgU5r/smartdevice/thing/service/property/set"

#define BYTE0(dwTemp)       (*( char *)(&dwTemp))
#define BYTE1(dwTemp)       (*((char *)(&dwTemp) + 1))
#define BYTE2(dwTemp)       (*((char *)(&dwTemp) + 2))
#define BYTE3(dwTemp)       (*((char *)(&dwTemp) + 3))
	

//MQTT连接服务器
extern int32_t mqtt_connect(char *client_id,char *user_name,char *password);

//MQTT消息订阅
extern int32_t mqtt_subscribe_topic(char *topic,uint8_t qos,uint8_t whether);

//MQTT消息发布
extern uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos);

//MQTT发送心跳包
extern void mqtt_send_heart(void);

extern int32_t esp8266_mqtt_init(void);

extern void mqtt_report_devices_status(void);

#endif
