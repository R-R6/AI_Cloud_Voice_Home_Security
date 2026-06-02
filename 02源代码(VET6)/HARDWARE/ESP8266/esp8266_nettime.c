#include "includes.h"

/*
 * ESP8266 HTTP 网络对时：GET www.beijing-time.org/time15.asp
 * 使用 CIPMUX=1 下 link 1 短连接，与 AT+MQTT* 并行；持 esp8266_uart_lock 访问串口。
 */

#define NETTIME_LINK_ID   1
#define NETTIME_HOST      "www.beijing-time.org"
#define NETTIME_PORT      80

static const char s_http_get1[] =
	"GET /time15.asp HTTP/1.1\r\n"
	"Host:www.beijing-time.org\r\n";
static const char s_http_get2[] = "...........\r\n";

static char s_cmd[96];
static char s_date_line[80];

/**
 * @brief 在 g_esp8266_rx_buf[g_esp8266_rx_cnt] 处补 '\0'，便于 strstr/sscanf 解析。
 *
 * @param 无
 */
static void nettime_rx_terminate(void)
{
	uint32_t n = g_esp8266_rx_cnt;

	if (n >= sizeof(g_esp8266_rx_buf))
		n = sizeof(g_esp8266_rx_buf) - 1U;
	g_esp8266_rx_buf[n] = '\0';
}

/**
 * @brief 将 0~99 的十进制数转为 BCD（供 RTC_SetTime/SetDate 使用）。
 *
 * @param[in] v 十进制数值（0~99）
 * @return BCD 编码字节
 */
static uint8_t dec_to_bcd(uint8_t v)
{
	return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

/**
 * @brief 将 HTTP Date 行中的三字母月份缩写转为 1~12。
 *
 * @param[in] m 指向 "Jan" 等 3 字符月份字符串
 * @return 1~12 表示月份；-1 表示无法识别
 */
static int month_from_str(const char *m)
{
	static const char *mons[] = {
		"Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec"
	};
	unsigned i;

	for (i = 0; i < 12U; i++)
	{
		if (strncmp(m, mons[i], 3) == 0)
			return (int)(i + 1U);
	}
	return -1;
}

/**
 * @brief 将 HTTP Date 行中的三字母星期缩写转为 RTC 星期值 1~7。
 *
 * @param[in] w 指向 "Mon" 等 3 字符星期字符串
 * @return 1=周一 … 7=周日；无法识别时默认 1
 */
static int weekday_from_str(const char *w)
{
	static const char *days[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
	unsigned i;

	for (i = 0; i < 7U; i++)
	{
		if (strncmp(w, days[i], 3) == 0)
			return (int)(i + 1U);
	}
	return 1;
}

/**
 * @brief 返回指定公历年月的天数（含闰年 2 月处理）。
 *
 * @param[in] year 四位公历年份
 * @param[in] mon  月份 1~12
 * @return 该月天数
 */
static int days_in_month(int year, int mon)
{
	static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	int d = dim[mon - 1];

	if (mon == 2)
	{
		int leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
		if (leap)
			d = 29;
	}
	return d;
}

/**
 * @brief 将 GMT 时刻加 8 小时转为北京时间，并处理日/月/年/星期进位。
 *
 * @param[in,out] year  年
 * @param[in,out] mon   月
 * @param[in,out] day   日
 * @param[in,out] hour  时
 * @param[in,out] min   分（本函数未修改，保留接口）
 * @param[in,out] sec   秒（本函数未修改，保留接口）
 * @param[in,out] wday  星期 1~7
 */
static void apply_cst_offset(int *year, int *mon, int *day, int *hour,
	int *min, int *sec, int *wday)
{
	int day0 = *day;
	int w0 = *wday;

	(void)min;
	(void)sec;

	*hour += 8;
	while (*hour >= 24)
	{
		*hour -= 24;
		(*day)++;
	}
	while (*day > days_in_month(*year, *mon))
	{
		*day -= days_in_month(*year, *mon);
		(*mon)++;
		if (*mon > 12)
		{
			*mon = 1;
			(*year)++;
		}
	}
	if (*day > day0)
	{
		*wday = w0 + 1;
		if (*wday > 7)
			*wday = 1;
	}
}

/**
 * @brief 从 HTTP 响应文本中截取完整 Date 行（从 "Date:" 到 "GMT" 含）。
 *
 * @param[in]  rx     HTTP 响应缓冲区（以 '\0' 结尾）
 * @param[out] out    输出 Date 行字符串
 * @param[in]  out_sz out 缓冲区容量（含 '\0'）
 * @return 0 截取成功；-1 未找到 Date 或 GMT，或行过短
 */
static int nettime_extract_date_line(const char *rx, char *out, unsigned out_sz)
{
	const char *start;
	const char *end;
	unsigned i = 0;

	start = strstr(rx, "Date:");
	if (start == NULL)
		start = strstr(rx, "Date: ");
	if (start == NULL)
		return -1;

	end = strstr(start, "GMT");
	if (end == NULL)
		return -1;
	end += 3;

	while (start < end && (i + 1U) < out_sz)
		out[i++] = *start++;

	out[i] = '\0';
	return (i >= 28U) ? 0 : -1;
}

/**
 * @brief 解析 HTTP Date 行，GMT+8 后写入全局 RTC_TimeStructure / RTC_DateStructure 并 SetTime/SetDate。
 *
 * @param[in] date_line 形如 "Date: Fri, 22 May 2026 19:36:00 GMT" 的字符串
 * @return 0 解析并写 RTC 成功；-1 格式错误或 RTC 写入失败
 */
static int nettime_parse_and_set_rtc(const char *date_line)
{
	char wday_str[4];
	char mon_str[4];
	int day, year, hour, min, sec, mon, wday;

	if (sscanf(date_line, "Date: %3s, %d %3s %d %d:%d:%d",
			wday_str, &day, mon_str, &year, &hour, &min, &sec) != 7)
		return -1;
	if (year < 2020 || year > 2099)
		return -1;

	mon = month_from_str(mon_str);
	if (mon < 0)
		return -1;

	wday = weekday_from_str(wday_str);
	/* HTTP Date 为 GMT，+8 为北京时间（如 19:25 GMT -> 次日 03:25） */
	apply_cst_offset(&year, &mon, &day, &hour, &min, &sec, &wday);

	RTC_DateStructure.RTC_Year    = dec_to_bcd((uint8_t)(year - 2000));
	RTC_DateStructure.RTC_Month   = dec_to_bcd((uint8_t)mon);
	RTC_DateStructure.RTC_Date    = dec_to_bcd((uint8_t)day);
	RTC_DateStructure.RTC_WeekDay = dec_to_bcd((uint8_t)wday);

	RTC_TimeStructure.RTC_H12     = RTC_H12_AM;
	RTC_TimeStructure.RTC_Hours   = dec_to_bcd((uint8_t)hour);
	RTC_TimeStructure.RTC_Minutes = dec_to_bcd((uint8_t)min);
	RTC_TimeStructure.RTC_Seconds = dec_to_bcd((uint8_t)sec);

	if (RTC_SetDate(RTC_Format_BCD, &RTC_DateStructure) != SUCCESS)
		return -1;
	if (RTC_SetTime(RTC_Format_BCD, &RTC_TimeStructure) != SUCCESS)
		return -1;

	RTC_WaitForSynchro();
	return 0;
}

/**
 * @brief 轮询等待 ESP8266 接收缓冲中出现 "GMT" 且字节计数稳定约 60ms。
 *
 * @param[in] timeout_ms 最大等待毫秒数
 * @return 0 已收到完整 Date 行标志；-1 超时
 */
static int nettime_wait_http_date(uint32_t timeout_ms)
{
	uint32_t last_cnt = 0;
	uint32_t stable_ms = 0;

	while (timeout_ms--)
	{
		nettime_rx_terminate();

		if (strstr((const char *)g_esp8266_rx_buf, "GMT") != NULL)
		{
			stable_ms++;
			if (stable_ms >= 60U)
				return 0;
		}
		else
			stable_ms = 0;

		if (g_esp8266_rx_cnt > 0U && g_esp8266_rx_cnt == last_cnt)
		{
		}
		else
			last_cnt = g_esp8266_rx_cnt;

		delay_ms(1);
	}
	return -1;
}

/**
 * @brief 关闭 NETTIME_LINK_ID 对应的 TCP 连接（AT+CIPCLOSE）。
 *
 * @param 无
 */
static void nettime_close_link(void)
{
	snprintf(s_cmd, sizeof(s_cmd), "AT+CIPCLOSE=%d\r\n", NETTIME_LINK_ID);
	esp8266_send_at(s_cmd);
	(void)esp8266_find_str_in_rx_packet("OK", 3000);
}

/**
 * @brief 经 ESP8266 向 www.beijing-time.org 发起 HTTP GET，解析 Date 头并同步 STM32 RTC（北京时间）。
 *
 * @param 无
 * @return 0 对时成功；-1 连接/收发/解析/写 RTC 失败（RTC 保持原值）
 *
 * @note 内部最多重试 3 次；成功时调用 rtc_notify_oled_refresh()；全程持有 esp8266_uart_lock。
 */
int32_t esp8266_nettime_sync(void)
{
	unsigned int pay_len;
	int32_t rt = -1;
	uint8_t retry;

	pay_len = (unsigned int)(strlen(s_http_get1) + strlen(s_http_get2));

	esp8266_uart_lock();

	for (retry = 0; retry < 3U; retry++)
	{
		snprintf(s_cmd, sizeof(s_cmd), "AT+CIPCLOSE=%d\r\n", NETTIME_LINK_ID);
		esp8266_send_at(s_cmd);
		(void)esp8266_find_str_in_rx_packet("OK", 1500);

		snprintf(s_cmd, sizeof(s_cmd),
			"AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n",
			NETTIME_LINK_ID, NETTIME_HOST, NETTIME_PORT);
		esp8266_send_at(s_cmd);
		if (esp8266_find_str_in_rx_packet("OK", 10000) != 0)
		{
			if (esp8266_find_str_in_rx_packet("CONNECT", 3000) != 0)
			{
				dgb_printf_safe("esp8266_nettime: CIPSTART fail try=%u\r\n",
					(unsigned)retry);
				continue;
			}
		}

		snprintf(s_cmd, sizeof(s_cmd), "AT+CIPSEND=%d,%u\r\n",
			NETTIME_LINK_ID, pay_len);
		esp8266_send_str(s_cmd);
		if (esp8266_find_str_in_rx_packet(">", 8000) != 0)
		{
			dgb_printf_safe("esp8266_nettime: no CIPSEND> try=%u\r\n",
				(unsigned)retry);
			nettime_close_link();
			continue;
		}

		g_esp8266_rx_cnt = 0;
		memset((void *)g_esp8266_rx_buf, 0, sizeof(g_esp8266_rx_buf));

		esp8266_send_str((char *)s_http_get1);
		esp8266_send_str((char *)s_http_get2);

		if (nettime_wait_http_date(15000) != 0)
		{
			nettime_rx_terminate();
			dgb_printf_safe("esp8266_nettime: wait GMT timeout try=%u rx=%lu\r\n",
				(unsigned)retry, (unsigned long)g_esp8266_rx_cnt);
			nettime_close_link();
			continue;
		}

		nettime_rx_terminate();

		if (nettime_extract_date_line((const char *)g_esp8266_rx_buf,
				s_date_line, sizeof(s_date_line)) != 0)
		{
			dgb_printf_safe("esp8266_nettime: extract fail try=%u rx=%lu\r\n",
				(unsigned)retry, (unsigned long)g_esp8266_rx_cnt);
			nettime_close_link();
			continue;
		}

		if (nettime_parse_and_set_rtc(s_date_line) == 0)
		{
			rt = 0;
			dgb_printf_safe("esp8266_nettime_sync OK [%s] -> Beijing %02d:%02d:%02d\r\n",
				s_date_line,
				(int)((RTC_TimeStructure.RTC_Hours >> 4) * 10 + (RTC_TimeStructure.RTC_Hours & 0x0F)),
				(int)((RTC_TimeStructure.RTC_Minutes >> 4) * 10 + (RTC_TimeStructure.RTC_Minutes & 0x0F)),
				(int)((RTC_TimeStructure.RTC_Seconds >> 4) * 10 + (RTC_TimeStructure.RTC_Seconds & 0x0F)));
			rtc_notify_oled_refresh();
			break;
		}

		dgb_printf_safe("esp8266_nettime: parse fail [%s]\r\n", s_date_line);
		nettime_close_link();
	}

	if (rt != 0)
		dgb_printf_safe("esp8266_nettime_sync fail\r\n");

	nettime_close_link();

	if (strstr((const char *)g_esp8266_rx_buf, "+MQTTSUBRECV") == NULL)
	{
		memset((void *)g_esp8266_rx_buf, 0, sizeof(g_esp8266_rx_buf));
		g_esp8266_rx_cnt = 0;
	}

	esp8266_uart_unlock();
	return rt;
}
