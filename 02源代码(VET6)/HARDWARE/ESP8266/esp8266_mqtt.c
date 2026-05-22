#include "includes.h"

/*
 * esp8266_mqtt.c —— ESP8266（安信可等）MQTT AT 固件封装。
 * 要点：
 *   - 发送 AT 后通过 USART3 中断累积 g_esp8266_rx_buf，用“计数静止”或子串扫描判断完成。
 *   - 长 JSON 发布走 MQTTPUBRAW，短负载可走 MQTTPUB（JSON 内引号需转义）。
 *   - 大量字符串放静态区，避免占用 app_task_esp8266 等任务的栈空间。
 */

// ==================== ESP8266 AT指令MQTT模式 ====================
// 注意：g_esp8266_rx_buf、g_esp8266_rx_cnt、g_esp8266_tx_buf 已在 usart.h 中定义，此处不再重复定义

/* OneNET 属性上报组包缓冲区（与 sprintf 格式长度匹配，勿随意缩小） */
char  g_mqtt_msg[526];

// 本地接收计数缓存（用于判断接收完成）
static uint16_t g_esp8266_rx_cnt_pre = 0;

/* 长 AT 行（MQTTUSERCFG 等）放静态区，减轻 app_task_esp8266 栈压力 */
static char s_esp8266_cmd_buf[512];
/* MQTT 发布：转义后的 JSON、整行 AT（避免栈过大） */
static char s_mqtt_esc[1100];
static char s_mqtt_pub_line[1200];
/* property/set_reply 专用短 JSON，避免与 g_mqtt_msg 周期上报互相覆盖 */
static char s_mqtt_set_reply[96];
/* PUBRAW 命令头放静态区，避免 app_task_mqtt 栈仅 512 字时局部 hdr[384] 溢出 */
static char s_pubraw_hdr[384];

/* ESP8266 单条 AT 输入长度上限约 256，超长则不要用 AT+MQTTPUB，直接 PUBRAW */
#define ESP8266_AT_LINE_SAFE_MAX 230

/**
 * @brief 获取 ESP8266 串口互斥锁（与 app_task_monitor / nettime 共用）。
 */
void esp8266_uart_lock(void)
{
	if (g_mutex_esp8266 != NULL)
		(void)xSemaphoreTake(g_mutex_esp8266, portMAX_DELAY);
}

/**
 * @brief 释放 ESP8266 串口互斥锁。
 */
void esp8266_uart_unlock(void)
{
	if (g_mutex_esp8266 != NULL)
		(void)xSemaphoreGive(g_mutex_esp8266);
}

/**
 * @brief 判断是否为 OneNET property/set 的 MQTT 下行（+MQTTSUBRECV 主题段）。
 */
static int mqtt_frame_is_property_set(const char *frame)
{
	if (frame == NULL)
		return 0;
	if (strstr(frame, "thing/property/set_reply") != NULL)
		return 0;
	if (strstr(frame, "thing/property/set") != NULL)
		return 1;
	return 0;
}

/**
 * @brief 从 +MQTTSUBRECV 整帧或纯 JSON 中定位 JSON 正文起始（首个 '{'）。
 */
static const char *mqtt_frame_json_body(const char *frame)
{
	const char *p;

	if (frame == NULL)
		return NULL;
	p = strchr(frame, '{');
	if (p != NULL)
		return p;
	return frame;
}

/**
 * @brief 在当前 USART3 接收快照中查找子串（不等待“静止”）。
 *
 * 将 g_esp8266_rx_buf 前 n 字节拷贝到本地缓冲并补 \\0 后调用 strstr，
 * 用于 MQTTPUBRAW 等场景：模块可能分包回显，仅靠字节计数不变会误判失败。
 *
 * @param[in] sub 要查找的 ASCII 子串（如 "OK\\r\\n"、">"）。
 *
 * @retval 1 已找到子串。
 * @retval 0 未找到或当前接收长度为 0。
 */
static int mqtt_rx_contains(const char *sub)
{
	static char scan[512];
	unsigned int n;

	n = (unsigned int)g_esp8266_rx_cnt;
	if (n == 0u)
		return 0;
	if (n >= sizeof(scan))
		n = (unsigned int)sizeof(scan) - 1u;
	memcpy(scan, (const void *)g_esp8266_rx_buf, n);
	scan[n] = '\0';
	return (strstr(scan, sub) != NULL);
}

#define REV_OK      0
#define REV_WAIT    1

/**
 * @brief 调试输出：打印当前 g_esp8266_rx_cnt 及接收缓冲前若干字节的十六进制。
 *
 * 在 AT 同步、MQTTUSERCFG 等失败路径调用，用于区分“模块无应答”（rx_cnt==0）
 * 与“有数据但无 OK”（接线/波特率/固件响应异常）。
 *
 * @param[in] tag 日志标签字符串，便于串口日志检索。
 */
static void esp8266_dbg_rx(const char *tag)
{
	unsigned int i, n = (unsigned int)g_esp8266_rx_cnt;

	dgb_printf_safe("esp8266_dbg[%s] rx_cnt=%lu\r\n", tag, (unsigned long)g_esp8266_rx_cnt);
	if (n > 48U)
		n = 48U;
	for (i = 0; i < n; i++)
		dgb_printf_safe(" %02X", (unsigned)g_esp8266_rx_buf[i]);
	if (n != 0U)
		dgb_printf_safe("\r\n");
}

/**
 * @brief 清空 ESP8266 串口接收缓冲与内部“前次计数”状态。
 *
 * 在发送新 AT 前或判定一次交互结束后调用，避免旧数据干扰 strstr 匹配。
 *
 * @note 与 ESP8266_WaitRecive 配合：WaitRecive 在两次调用间若计数不变则判为收完，
 *       本函数会同时复位 g_esp8266_rx_cnt_pre。
 */
void ESP8266_Clear(void)
{
	memset((void *)g_esp8266_rx_buf, 0, sizeof(g_esp8266_rx_buf));
	g_esp8266_rx_cnt = 0;
	g_esp8266_rx_cnt_pre = 0;
}

/**
 * @brief 基于接收字节计数是否“静止”判断一帧是否接收完成（非阻塞）。
 *
 * 典型用法：在循环中每隔 tick 调用；若本次 g_esp8266_rx_cnt 与上次保存值相同且非 0，
 * 则认为本段接收已稳定，可配合 strstr 解析。
 *
 * @retval REV_OK(0) 接收计数非零且与上一快照相同，可认为本帧结束。
 * @retval REV_WAIT(1) 尚无数据或计数仍在增长。
 *
 * @note 本函数不清空 g_esp8266_rx_buf；清空由 ESP8266_Clear 在适当时机完成。
 */
unsigned char ESP8266_WaitRecive(void)
{
	if(g_esp8266_rx_cnt == 0)
		return REV_WAIT;

	if(g_esp8266_rx_cnt == g_esp8266_rx_cnt_pre)
		return REV_OK;

	g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
	return REV_WAIT;
}

/**
 * @brief 发送一条 AT 指令并轮询接收缓冲直至出现期望子串或超时。
 *
 * 每次循环延时约 10 ms（vTaskDelay(pdMS_TO_TICKS(10))），总超时约为 polls*10 ms。
 * 若在等待过程中收到数据但不含 res，会清空缓冲并继续等待（与模块持续吐 URC 的行为适配）。
 *
 * @param[in] cmd 以 \\r\\n 结尾的完整 AT 字符串。
 * @param[in] res 期望在响应中出现的子串（通常为 "OK"）。
 * @param[in] polls 最大循环次数。
 *
 * @retval 0 在超时前收到包含 res 的响应。
 * @retval 1 超时未匹配到 res。
 */
unsigned char ESP8266_SendCmdPolls(char *cmd, char *res, unsigned int polls)
{
	unsigned int timeOut = polls;

	ESP8266_Clear();
	g_esp8266_rx_cnt_pre = 0;

	esp8266_send_bytes((uint8_t *)cmd, strlen(cmd));

	while(timeOut--)
	{
		if(ESP8266_WaitRecive() == REV_OK)
		{
			if(strstr((const char *)g_esp8266_rx_buf, res) != NULL)
			{
				ESP8266_Clear();
				g_esp8266_rx_cnt_pre = 0;
				return 0;
			}
			/*
			 * 未匹配到 OK 时不能 Clear：缓冲里可能是 +MQTTSUBRECV(property/set)，
			 * 清掉会导致云端下发丢失，平台报属性设置超时。
			 */
			g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	/* 超时：若已有 MQTT 下行 URC，留给 app_task_monitor 投递 */
	if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
	{
		ESP8266_Clear();
		g_esp8266_rx_cnt_pre = 0;
	}
	return 1;
}

/**
 * @brief 发送 AT 并等待响应，使用默认较长超时（约 8 s）。
 *
 * 等价于 ESP8266_SendCmdPolls(cmd, res, 800)。
 *
 * @param[in] cmd AT 指令字符串。
 * @param[in] res 期望子串。
 *
 * @retval 0 成功匹配；非 0 表示失败（与 SendCmdPolls 一致）。
 */
unsigned char ESP8266_SendCmd(char *cmd, char *res)
{
	return ESP8266_SendCmdPolls(cmd, res, 800U);
}

/**
 * @brief 通过 AT+MQTTCLEAN 释放 link_id=0 上的 MQTT 会话。
 *
 * 在复位 MCU 而模块未掉电、或重复初始化前调用，避免旧会话占用导致连接异常。
 *
 * @note 内部使用 ESP8266_SendCmd，失败时仍可能返回无日志；上层可结合重试。
 */
void mqtt_disconnect()
{
	ESP8266_SendCmd("AT+MQTTCLEAN=0\r\n", "OK");
}

/**
 * @brief 发送 AT+MQTTPING，向 Broker 维持 MQTT 会话活跃。
 *
 * 是否仍被云端断开还取决于模块侧 keepalive 与网络状况；本函数仅发起一次 PING。
 */
void mqtt_send_heart(void)
{
	esp8266_uart_lock();
	ESP8266_SendCmd("AT+MQTTPING\r\n", "OK");
	esp8266_uart_unlock();
}

/**
 * @brief 清空收发缓冲并两次调用 mqtt_disconnect，用于 MQTT 栈侧“软复位”入口。
 *
 * @param[in] prx  保留参数，与历史接口一致。
 * @param[in] rxlen 保留参数，与历史接口一致。
 * @param[in] ptx  保留参数，与历史接口一致。
 * @param[in] txlen 保留参数，与历史接口一致。
 *
 * @note 当前实现未使用 prx/rxlen/ptx/txlen，仅操作全局 g_esp8266_tx_buf 与 g_esp8266_rx_buf。
 */
void mqtt_init(uint8_t *prx,uint16_t rxlen,uint8_t *ptx,uint16_t txlen)
{
	memset(g_esp8266_tx_buf,0,sizeof(g_esp8266_tx_buf)); //清空发送缓冲
	memset((void *)g_esp8266_rx_buf,0,sizeof(g_esp8266_rx_buf)); //清空接收缓冲

	// 无条件先主动断开
	mqtt_disconnect();
	delay_ms(100);
	
	mqtt_disconnect();
	delay_ms(100);
}

/**
 * @brief 配置 MQTT 用户参数并连接 Broker（AT+MQTTUSERCFG / AT+MQTTCONN）。
 *
 * 适用于与 esp8266_mqtt_init 分离、仅重复建链的场景；鉴权字段与 OneNET 新版物模型一致。
 *
 * @param[in] client_id  对应 MQTT 客户端 ID（本工程常为设备名）。
 * @param[in] user_name  对应 MQTT 用户名（本工程常为产品 ID）。
 * @param[in] password   对应 MQTT 密码（token 字符串）。
 *
 * @retval 0  连接成功。
 * @retval -1 USERCFG 或 CONN 在重试后仍失败。
 */
int32_t mqtt_connect(char *client_id,char *user_name,char *password)
{
	uint32_t cnt = 3;  // 重试3次
	char cmd_buf[256];
	
	// 步骤1: 配置MQTT用户参数（鉴权信息）
	sprintf(cmd_buf, 
		"AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
		client_id, user_name, password);
	
	dgb_printf_safe("Step1: Config MQTT User...\r\n");
	while(cnt--)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT User Config Success\r\n");
			break;
		}
		else
		{
			dgb_printf_safe("MQTT User Config Fail, Retrying...\r\n");
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
	
	if(cnt == 0)
	{
		dgb_printf_safe("MQTT User Config Failed after 3 retries\r\n");
		return -1;
	}
	
	// 步骤2: 建立MQTT连接
	sprintf(cmd_buf, 
		"AT+MQTTCONN=0,\"%s\",%d,0\r\n",
		MQTT_BROKERADDRESS, MQTT_PORT);
	
	cnt = 3;  // 重试3次
	dgb_printf_safe("Step2: Connect to MQTT Broker...\r\n");
	while(cnt--)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT Connect Success\r\n");
			return 0;  // 连接成功
		}
		else
		{
			dgb_printf_safe("MQTT Connect Fail, Retrying...\r\n");
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
	
	dgb_printf_safe("MQTT Connect Failed after 3 retries\r\n");
	return -1;
}

/**
 * @brief 订阅或取消订阅指定 MQTT 主题（AT+MQTTSUB / AT+MQTTUNSUB）。
 *
 * @param[in] topic   完整主题字符串。
 * @param[in] qos     订阅 QoS，取 0 或 1（与 AT 固件约定一致）。
 * @param[in] whether 1 表示订阅；0 表示取消订阅。
 *
 * @retval 0  操作成功（收到 OK）。
 * @retval -1 重试后仍失败。
 */
int32_t mqtt_subscribe_topic(char *topic,uint8_t qos,uint8_t whether)
{
	char cmd_buf[256];
	uint32_t cnt = 3;
	
	if(whether)
	{
		// 订阅主题
		sprintf(cmd_buf, 
			"AT+MQTTSUB=0,\"%s\",%d\r\n",
			topic, qos);
		
		dgb_printf_safe("Subscribe Topic: %s\r\n", topic);
		while(cnt--)
		{
			if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
			{
				dgb_printf_safe("Subscribe Success\r\n");
				return 0;
			}
			else
			{
				dgb_printf_safe("Subscribe Fail, Retrying...\r\n");
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
	}
	else
	{
		// 取消订阅
		sprintf(cmd_buf, 
			"AT+MQTTUNSUB=0,\"%s\"\r\n",
			topic);
		
		dgb_printf_safe("Unsubscribe Topic: %s\r\n", topic);
		while(cnt--)
		{
			if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
			{
				dgb_printf_safe("Unsubscribe Success\r\n");
				return 0;
			}
			else
			{
				dgb_printf_safe("Unsubscribe Fail, Retrying...\r\n");
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
	}
	
	return -1;
}

/**
 * @brief 将 JSON 文本转义为可嵌入 AT+MQTTPUB 双引号参数内的形式。
 *
 * 对 \\、" 前插入反斜杠；对逗号转义为 \\,（ESP8266 AT 以逗号分隔参数，否则 ERROR）。
 *
 * @param[in]  src    原始 JSON（UTF-8/ASCII）。
 * @param[out] dst    输出缓冲。
 * @param[in]  dst_sz dst 最大容量（含结尾 \\0）。
 *
 * @retval >=0 转义后字符串长度（不含 \\0）。
 * @retval -1  缓冲不足或无法完整写入结尾 \\0。
 */
static int mqtt_at_escape_payload(const char *src, char *dst, size_t dst_sz)
{
	size_t i, j = 0;

	for (i = 0; src[i] != '\0'; i++)
	{
		if (src[i] == ',')
		{
			if (j + 2 >= dst_sz)
				return -1;
			dst[j++] = '\\';
			dst[j++] = ',';
			continue;
		}
		if (src[i] == '\\' || src[i] == '"')
		{
			if (j + 2 >= dst_sz)
				return -1;
			dst[j++] = '\\';
		}
		if (j + 1 >= dst_sz)
			return -1;
		dst[j++] = src[i];
	}
	if (j >= dst_sz)
		return -1;
	dst[j] = '\0';
	return (int)j;
}

/**
 * @brief 轮询等待 AT 发布成功（OK 或 +MQTTPUB:OK）。
 *
 * @param[in] polls 最大轮询次数（约 polls*10 ms）。
 *
 * @retval 0 成功。
 * @retval 1 超时。
 */
static unsigned char mqtt_wait_publish_ok(unsigned int polls)
{
	unsigned int n = polls;

	while (n--)
	{
		if (ESP8266_WaitRecive() == REV_OK)
		{
			if (strstr((const char *)g_esp8266_rx_buf, "OK") != NULL ||
			    strstr((const char *)g_esp8266_rx_buf, "+MQTTPUB:OK") != NULL)
				return 0;
			if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
				ESP8266_Clear();
			else
				g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	return 1;
}

/**
 * @brief 向 property/set_reply 发布短 JSON（清空 RX 后走 AT+MQTTPUB，不走 PUBRAW）。
 *
 * @param[in] id  与下行 property/set 中 id 一致。
 *
 * @retval >0 成功（返回 JSON 长度）。
 * @retval 0  失败。
 */
static uint32_t mqtt_publish_set_reply(const char *id)
{
	int n;
	int esc_len;
	uint32_t msg_len;

	if (id == NULL)
		return 0u;

	snprintf(s_mqtt_set_reply, sizeof(s_mqtt_set_reply),
		"{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id);
	msg_len = (uint32_t)strlen(s_mqtt_set_reply);

	esp8266_uart_lock();
	ESP8266_Clear();
	g_esp8266_rx_cnt_pre = 0;

	esc_len = mqtt_at_escape_payload(s_mqtt_set_reply, s_mqtt_esc, sizeof(s_mqtt_esc));
	if (esc_len < 0)
	{
		esp8266_uart_unlock();
		return 0u;
	}

	n = snprintf(s_mqtt_pub_line, sizeof(s_mqtt_pub_line),
		"AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n",
		MQTT_SET_REPLY_TOPIC, s_mqtt_esc);
	if (n <= 0 || (unsigned int)n >= sizeof(s_mqtt_pub_line))
	{
		esp8266_uart_unlock();
		return 0u;
	}

	esp8266_send_bytes((uint8_t *)s_mqtt_pub_line, (uint32_t)n);

	if (mqtt_wait_publish_ok(400U) == 0)
	{
		ESP8266_Clear();
		g_esp8266_rx_cnt_pre = 0;
		esp8266_uart_unlock();
		return msg_len;
	}

	esp8266_uart_unlock();
	return 0u;
}

/**
 * @brief 使用 AT+MQTTPUBRAW 发布指定长度原始负载（适合长 JSON）。
 *
 * 安信可 AT 2.2.x 常见两种行为：先回 '>' 再发负载，或仅回 OK 后立刻发送原始字节。
 * 本实现通过 mqtt_rx_contains 轮询 ">" 或 "OK" 及 ERROR，再延时后发送 message。
 *
 * @param[in] topic    发布主题。
 * @param[in] message  负载首指针（二进制安全，长度为 msg_len）。
 * @param[in] qos      MQTT QoS（0 或 1）。
 * @param[in] msg_len  负载字节数。
 *
 * @retval msg_len 发布流程在响应中看到 OK 视为成功，返回与入参相同的长度。
 * @retval 0       任一步超时或出现 ERROR。
 */
static uint32_t mqtt_publish_pubraw(char *topic, char *message, uint8_t qos, unsigned int msg_len)
{
	unsigned int hl;
	unsigned int polls;

	hl = (unsigned int)snprintf(s_pubraw_hdr, sizeof(s_pubraw_hdr),
		"AT+MQTTPUBRAW=0,\"%s\",%u,%u,0\r\n",
		topic, msg_len, (unsigned int)qos);
	if (hl <= 0u || hl >= sizeof(s_pubraw_hdr))
		return 0u;

	/* 若缓冲中已有 MQTT 下行 URC，勿 Clear，留给 monitor 投递 property/set */
	if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
	{
		ESP8266_Clear();
		g_esp8266_rx_cnt_pre = 0;
	}
	esp8266_send_bytes((uint8_t *)s_pubraw_hdr, hl);

	for (polls = 0u; polls < 600u; polls++)
	{
		if ((mqtt_rx_contains("\r\nERROR\r\n") || mqtt_rx_contains("ERROR\r\n")) &&
		    !mqtt_rx_contains("+MQTTSUBRECV"))
		{
			dgb_printf_safe("MQTT Publish Failed (PUBRAW cmd ERROR)\r\n");
			ESP8266_Clear();
			return 0u;
		}
		if (mqtt_rx_contains(">"))
			break;
		if (mqtt_rx_contains("OK\r\n") || mqtt_rx_contains("\r\nOK"))
			break;

		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (polls >= 600u)
	{
		dgb_printf_safe("MQTT Publish Failed (PUBRAW no OK/>)\r\n");
		if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
			ESP8266_Clear();
		return 0u;
	}

	if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
	{
		ESP8266_Clear();
		g_esp8266_rx_cnt_pre = 0;
	}
	vTaskDelay(pdMS_TO_TICKS(30));
	esp8266_send_bytes((uint8_t *)message, msg_len);

	for (polls = 0u; polls < 800u; polls++)
	{
		if ((mqtt_rx_contains("\r\nERROR\r\n") || mqtt_rx_contains("ERROR\r\n")) &&
		    !mqtt_rx_contains("+MQTTSUBRECV"))
			break;
		if (mqtt_rx_contains("OK\r\n") || mqtt_rx_contains("\r\nOK"))
		{
			if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
			{
				ESP8266_Clear();
				g_esp8266_rx_cnt_pre = 0;
			}
			else
				g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
			return (uint32_t)msg_len;
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}

	dgb_printf_safe("MQTT Publish Failed (PUBRAW finish)\r\n");
	if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
		ESP8266_Clear();
	return 0u;
}

/**
 * @brief 向指定主题发布 MQTT 消息（优先 AT+MQTTPUB，过长则 AT+MQTTPUBRAW）。
 *
 * 流程：对 message 转义后拼入 AT+MQTTPUB；若转义失败、snprintf 溢出或整行超过
 * ESP8266_AT_LINE_SAFE_MAX，则回退到 mqtt_publish_pubraw。
 *
 * @param[in] topic   发布主题。
 * @param[in] message 以 \\0 结尾的 UTF-8/ASCII 负载（通常为 JSON）。
 * @param[in] qos     QoS 0 或 1。
 *
 * @retval >0 成功时返回 strlen(message)（与历史行为一致）。
 * @retval 0  topic/message 非法、长度为 0、超过 g_mqtt_msg、或发布失败。
 */
uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos)
{
	unsigned int msg_len;
	int n;
	uint32_t rt = 0u;

	if (topic == NULL || message == NULL)
		return 0;
	msg_len = (unsigned int)strlen(message);
	if (msg_len == 0u || msg_len > sizeof(g_mqtt_msg))
		return 0;

	esp8266_uart_lock();

	if (mqtt_at_escape_payload(message, s_mqtt_esc, sizeof(s_mqtt_esc)) < 0)
	{
		rt = mqtt_publish_pubraw(topic, message, qos, msg_len);
		esp8266_uart_unlock();
		return rt;
	}

	n = snprintf(s_mqtt_pub_line, sizeof(s_mqtt_pub_line),
		"AT+MQTTPUB=0,\"%s\",\"%s\",%u,0\r\n",
		topic, s_mqtt_esc, (unsigned int)qos);
	if (n < 0 || (unsigned int)n >= sizeof(s_mqtt_pub_line))
	{
		rt = mqtt_publish_pubraw(topic, message, qos, msg_len);
		esp8266_uart_unlock();
		return rt;
	}

	/* 过长时发 AT+MQTTPUB 会被模块截断，表现为无 OK / 异常 */
	if ((unsigned int)n <= ESP8266_AT_LINE_SAFE_MAX)
	{
		if (ESP8266_SendCmdPolls(s_mqtt_pub_line, "OK", 800U) == 0)
			rt = (uint32_t)msg_len;
		else
			rt = mqtt_publish_pubraw(topic, message, qos, msg_len);
	}
	else
		rt = mqtt_publish_pubraw(topic, message, qos, msg_len);

	esp8266_uart_unlock();
	return rt;
}

/**
 * @brief 从 property/set 载荷中提取平台下发的 id，用于 set_reply 回显。
 *
 * 支持 "id":"abc" 与 "id":123；若不存在 id 字段则默认 "1"。
 */
static void mqtt_extract_set_request_id(const char *payload, char *id_buf, unsigned id_buf_sz)
{
	const char *p;
	unsigned j;

	if (id_buf == NULL || id_buf_sz < 2U)
		return;

	id_buf[0] = '1';
	id_buf[1] = '\0';

	p = strstr(payload, "\"id\"");
	if (p == NULL)
		return;

	p = strchr(p, ':');
	if (p == NULL)
		return;
	p++;

	while (*p == ' ' || *p == '\t')
		p++;

	if (*p == '\"')
	{
		p++;
		j = 0U;
		while (*p != '\0' && *p != '\"' && (j + 1U) < id_buf_sz)
			id_buf[j++] = *p++;
		id_buf[j] = '\0';
	}
	else
	{
		j = 0U;
		while (*p != '\0' && *p != ',' && *p != '}' && (j + 1U) < id_buf_sz)
			id_buf[j++] = *p++;
		id_buf[j] = '\0';
	}

	if (id_buf[0] == '\0')
	{
		id_buf[0] = '1';
		id_buf[1] = '\0';
	}
}

/**
 * @brief 解析单个 switch_led_x 的目标值（0/1），兼容扁平与 {"value":n} 两种物模型写法。
 *
 * @retval 0/1  解析成功。
 * @retval -1   载荷中无该键或格式无法识别。
 */
static int mqtt_parse_switch_value(const char *payload, const char *key)
{
	const char *p;

	p = strstr(payload, key);
	if (p == NULL)
		return -1;

	p = strchr(p, ':');
	if (p == NULL)
		return -1;
	p++;

	while (*p == ' ' || *p == '\t')
		p++;

	/* 新版： "switch_led_1":{"value":1} */
	if (*p == '{')
	{
		p = strstr(p, "value");
		if (p == NULL)
			return -1;
		p = strchr(p, ':');
		if (p == NULL)
			return -1;
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
	}

	if (*p == '1')
		return 1;
	if (*p == '0')
		return 0;

	return -1;
}

/**
 * @brief 解析 OneNET property/set 下行：更新三路 LED，并向 set_reply 主题应答。
 *
 * 平台同步属性设置会等待本应答，否则控制台报「设备响应超时」(10411)。
 * LED：低电平点亮（与 mqtt_report_devices_status / 原 app_task_esp8266 一致）。
 */
uint8_t mqtt_handle_property_set(char *payload)
{
	int v1, v2, v3;
	char id[16];
	uint32_t pub_len;
	const char *json;

	if (payload == NULL)
		return 0U;

	/* 只处理 property/set 主题的下发，忽略 post/reply 等 URC */
	if (!mqtt_frame_is_property_set(payload))
		return 0U;

	json = mqtt_frame_json_body(payload);
	if (json == NULL)
		return 0U;

	if (strstr(json, "switch_led_1") == NULL &&
	    strstr(json, "switch_led_2") == NULL &&
	    strstr(json, "switch_led_3") == NULL)
		return 0U;

	v1 = mqtt_parse_switch_value(json, "switch_led_1");
	v2 = mqtt_parse_switch_value(json, "switch_led_2");
	v3 = mqtt_parse_switch_value(json, "switch_led_3");

	if (v1 >= 0)
		PEout(11) = (v1 != 0) ? 0 : 1;
	if (v2 >= 0)
		PEout(12) = (v2 != 0) ? 0 : 1;
	if (v3 >= 0)
		PEout(13) = (v3 != 0) ? 0 : 1;

	mqtt_extract_set_request_id(json, id, sizeof(id));

	/* 专用 AT+MQTTPUB 应答（带 \\, 转义 + 清 RX），避免走 PUBRAW 失败 */
	pub_len = mqtt_publish_set_reply(id);

	dgb_printf_safe("mqtt property/set: led1=%d led2=%d led3=%d, set_reply id=%s %s\r\n",
		(v1 >= 0) ? v1 : -1, (v2 >= 0) ? v2 : -1, (v3 >= 0) ? v3 : -1,
		id, (pub_len != 0U) ? "OK" : "FAIL");

	return 1U;
}

/**
 * @brief 读取 GPIO 与全局变量，组装 OneNET 新版物模型属性 JSON 并发布到 MQTT_PUBLISH_TOPIC。
 *
 * 属性包括 temperature、Humidity、switch_led_1..3、fire、mq2；数值格式需与云端物模型标识符一致。
 * 发布 QoS 固定为 1（与工程内既有约定一致）。
 *
 * @note LED 状态经 GPIO 读脚后取反再填入 JSON，与硬件低电平点亮逻辑一致。
 */
void mqtt_report_devices_status(void)
{
	uint8_t led_1_sta = GPIO_ReadOutputDataBit(GPIOE,GPIO_Pin_11) ? 0:1;
	uint8_t led_2_sta = GPIO_ReadOutputDataBit(GPIOE,GPIO_Pin_12) ? 0:1;
	uint8_t led_3_sta = GPIO_ReadOutputDataBit(GPIOE,GPIO_Pin_13) ? 0:1;

	// OneNET新版物模型JSON格式
	// 注意：每个属性值必须包裹在 {"value": xxx} 中
	// 不包含 method 和 version 字段
	sprintf(g_mqtt_msg,
		"{\"id\":\"123\",\"params\":{"
			"\"temperature\":{\"value\":%.1f},"
			"\"Humidity\":{\"value\":%.1f},"
			"\"switch_led_1\":{\"value\":%d},"
			"\"switch_led_2\":{\"value\":%d},"
			"\"switch_led_3\":{\"value\":%d},"
			"\"fire\":{\"value\":%d},"
			"\"mq2\":{\"value\":%d}"
		"}}",
		g_temp,
		g_humi,
		led_1_sta,
		led_2_sta,
		led_3_sta,
		g_fire_status,
		g_mq2_status);

	/* QoS 1 与参考示例一致；云端未建属性时仍可 PUBLISH，平台侧可能丢弃或记日志，与 AT 无关 */
	mqtt_publish_data(MQTT_PUBLISH_TOPIC, g_mqtt_msg, 1);
}

/**
 * @brief 上电后完整初始化 ESP8266 并连接 OneNET MQTT：串口、退出透传、AT 同步、STA、
 *        DHCP、CWJAP、MQTTCLEAN、MQTTUSERCFG、MQTTCONN、订阅 property/set 与可选 post/reply。
 *
 * @retval 0   全部步骤成功。
 * @retval -2  AT 同步失败（硬件接线/波特率/模块未就绪等）。
 * @retval -3  ATE0 关闭回显失败。
 * @retval -4  AT+CWMODE=1 失败。
 * @retval -5  WiFi 连接未出现 GOT IP。
 * @retval -6  AT+MQTTUSERCFG 失败。
 * @retval -7  AT+MQTTCONN 失败。
 * @retval -8  订阅 MQTT_SUBSCRIBE_TOPIC 失败（MQTT_REPLY_TOPIC 失败不返回错误）。
 *
 * @note 本函数内含大量 delay 与 vTaskDelay，应在任务上下文调用，避免阻塞裸机主循环过久。
 */
int32_t esp8266_mqtt_init(void)
{
	int32_t rt;
	char *cmd_buf = s_esp8266_cmd_buf;
	uint8_t retry;
	uint8_t wifi_ok;
	uint8_t mqtt_conn_ok;
	uint8_t usercfg_ok;

	esp8266_init(115200);

	rt = esp8266_exit_transparent_transmission();
	if (rt != 0)
		dgb_printf_safe("esp8266_exit_transparent_transmission fail\r\n");
	dgb_printf_safe("esp8266_exit_transparent_transmission success\r\n");
	dgb_printf_safe("esp8266: pause 2s then AT sync...\r\n");
	delay_ms(2000);

	/* 单次短超时(约1.2s)+每轮打印，避免误以为“没有日志” */
	for (retry = 0; retry < 40; retry++)
	{
		dgb_printf_safe("esp8266 AT try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls("AT\r\n", "OK", 120U) == 0)
			break;
		if (((retry + 1U) % 10U) == 0U)
			esp8266_dbg_rx("AT no OK");
		vTaskDelay(pdMS_TO_TICKS(300));
	}
	if (retry >= 40)
	{
		esp8266_dbg_rx("AT sync final");
		dgb_printf_safe("esp8266 AT sync fail (查:PB10/11与模块RX/TX交叉、共地;勿接PC10/11 UART4)\r\n");
		return -2;
	}
	dgb_printf_safe("esp8266 AT sync OK\r\n");

	if (ESP8266_SendCmdPolls("ATE0\r\n", "OK", 200U) != 0)
	{
		dgb_printf_safe("esp8266_enable_echo(ATE0) fail\r\n");
		return -3;
	}
	dgb_printf_safe("esp8266_enable_echo(0) success\r\n");
	delay_ms(200);

	for (retry = 0; retry < 10; retry++)
	{
		dgb_printf_safe("esp8266 CWMODE try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls("AT+CWMODE=1\r\n", "OK", 200U) == 0)
			break;
		vTaskDelay(pdMS_TO_TICKS(500));
	}
	if (retry >= 10)
	{
		dgb_printf_safe("esp8266 CWMODE fail\r\n");
		return -4;
	}

	if (ESP8266_SendCmdPolls("AT+CWDHCP=1,1\r\n", "OK", 200U) != 0)
		dgb_printf_safe("esp8266 CWDHCP warn (ignored)\r\n");

	sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
	wifi_ok = 0;
	for (retry = 0; retry < 60; retry++)
	{
		dgb_printf_safe("esp8266 CWJAP try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls(cmd_buf, "GOT IP", 1200U) == 0)
		{
			wifi_ok = 1;
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	if (!wifi_ok)
	{
		dgb_printf_safe("esp8266 CWJAP / GOT IP fail\r\n");
		return -5;
	}
	dgb_printf_safe("esp8266 WiFi GOT IP\r\n");
	/* 多连接：MQTT 走 AT+MQTT*，HTTP 对时用 link 1，避免单连接模式抢占会话 */
	if (ESP8266_SendCmdPolls("AT+CIPMUX=1\r\n", "OK", 200U) != 0)
		dgb_printf_safe("esp8266 CIPMUX=1 warn (nettime may fail)\r\n");
	delay_ms(200);
	/* 协议栈稳定；仅复位 MCU 时模块侧 MQTT  session 可能仍在，必须先清理 */
	delay_ms(800);
	for (retry = 0; retry < 3; retry++)
	{
		ESP8266_SendCmdPolls("AT+MQTTCLEAN=0\r\n", "OK", 200U);
		vTaskDelay(pdMS_TO_TICKS(400));
	}
	delay_ms(300);

	sprintf(cmd_buf,
		"AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
		MQTT_CLIENTID, MQTT_USARNAME, MQTT_PASSWD);

	dgb_printf_safe("Config MQTT User...\r\n");
	usercfg_ok = 0;
	for (retry = 0; retry < 8; retry++)
	{
		dgb_printf_safe("MQTT User Config try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			usercfg_ok = 1;
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(600));
	}
	if (!usercfg_ok)
	{
		esp8266_dbg_rx("MQTTUSERCFG");
		dgb_printf_safe("MQTT User Config Fail\r\n");
		return -6;
	}
	dgb_printf_safe("MQTT User Config Success\r\n");
	delay_ms(1000);

	sprintf(cmd_buf,
		"AT+MQTTCONN=0,\"%s\",%d,0\r\n",
		MQTT_BROKERADDRESS, MQTT_PORT);

	mqtt_conn_ok = 0;
	dgb_printf_safe("Connect to MQTT Broker...\r\n");
	for (retry = 0; retry < 3; retry++)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT Connect Success\r\n");
			mqtt_conn_ok = 1;
			break;
		}
		dgb_printf_safe("MQTT Connect Fail, Retrying (%u)...\r\n", (unsigned)(retry + 1));
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	if(!mqtt_conn_ok)
	{
		dgb_printf_safe("MQTT Connect Failed after 3 retries\r\n");
		return -7;
	}
	
	delay_ms(1000);
	
	// 步骤8: 订阅云端下发指令主题
	if(mqtt_subscribe_topic(MQTT_SUBSCRIBE_TOPIC, 0, 1) != 0)
	{
		dgb_printf_safe("mqtt_subscribe_topic fail\r\n");
		return -8;
	}	
	dgb_printf_safe("mqtt_subscribe_topic success\r\n");
	
	// 步骤9: （可选）订阅上报回执主题
	if(mqtt_subscribe_topic(MQTT_REPLY_TOPIC, 0, 1) != 0)
	{
		dgb_printf_safe("Subscribe reply topic fail (optional)\r\n");
		// 不返回错误，这是可选的
	}
	
	dgb_printf_safe("===== ESP8266 MQTT Init Complete =====\r\n");
	return 0;
}
