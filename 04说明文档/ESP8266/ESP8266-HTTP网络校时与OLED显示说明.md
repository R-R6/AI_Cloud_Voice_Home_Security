# ESP8266 HTTP 网络校时与 OLED 显示说明

> **适合谁读**：已能 MQTT 上 OneNET，需要理解 **HTTP → GMT+8 → RTC → rtc_notify_oled_refresh → app_task_rtc → OLED** 全链路；**GMT/HTTP 协议基础**见第 3～5 节；踩坑见第 10 节。  
> **对应源码**：`HARDWARE/ESP8266/esp8266_nettime.c`、`esp8266_nettime.h`；`HARDWARE/RTC/rtc.c`；`USER/main.c` 中 `app_task_rtc` / `app_task_esp8266` / `app_task_mqtt`。  
> **参考例程**：温工 `tcp_client_ap_connect_time_demo1`（`GET /time15.asp` + 解析 `Date` 行）。  
> **与 MQTT 的关系**：见本文第 9 节；MQTT 任务说明见 [ESP8266-OneNET-MQTT与FreeRTOS任务说明.md](./ESP8266-OneNET-MQTT与FreeRTOS任务说明.md)。

---

## 1. 一分钟看懂：时间从哪来到哪去

**端到端主链路（建议先记住这一条）：**

```text
HTTP → GMT+8 → RTC → rtc_notify_oled_refresh → app_task_rtc → OLED
```

| 步骤 | 含义 |
|------|------|
| **HTTP** | `esp8266_nettime_sync()` 经 link 1 访问 `www.beijing-time.org`，解析响应头 `Date: ... GMT` |
| **GMT+8** | `apply_cst_offset(+8)` 转为北京时间 |
| **RTC** | `RTC_SetDate` / `RTC_SetTime` 写入 STM32 硬件 RTC |
| **rtc_notify_oled_refresh** | 置位 `EVENT_GROUP_RTC_WAKEUP`，通知 RTC 任务立即刷新（不读网、不画屏） |
| **app_task_rtc** | `RTC_GetTime` / `RTC_GetDate`，格式化为字符串 |
| **OLED** | 经 `g_queue_oled` 交给 `app_task_oled` 显示（UI 坐标与改前一致） |

以下为各步骤在工程中的展开说明：

```text
WiFi GOT IP + MQTT 建链成功
    → esp8266_nettime_sync()          // HTTP GET，link 1
        → 解析 HTTP 头 Date: ... GMT
        → GMT + 8 → 北京时间
        → RTC_SetDate / RTC_SetTime   // 写入 STM32 RTC
        → rtc_notify_oled_refresh()   // 置位事件，通知刷新

app_task_rtc（等 EVENT_GROUP_RTC_WAKEUP）
    → RTC_GetTime / RTC_GetDate       // 从 RTC 读
    → g_queue_oled → app_task_oled    // 主界面时间/日期/星期（坐标不变）

RTC 每秒唤醒中断
    → 同样置位 EVENT_GROUP_RTC_WAKEUP → 秒针走字
```

| 环节 | 作用 |
|------|------|
| `esp8266_nettime_sync()` | 经 ESP8266 发 HTTP，得到 GMT，转北京时间后 **写 RTC** |
| `rtc_notify_oled_refresh()` | **不读时间、不画屏**，只通知 `app_task_rtc` 立刻刷新 |
| `app_task_rtc` | 从 **RTC 寄存器** 读 BCD，按原 UI 坐标发 OLED 队列 |

**要点**：OLED 不直接显示 HTTP 字符串，而是显示 **RTC 里的值**；网络校时只是 RTC 的数据来源之一（蓝牙改时间仍写同一 RTC）。

---

## 2. 何时触发网络校时

| 时机 | 调用位置 | 说明 |
|------|----------|------|
| **首次** | `app_task_esp8266`：MQTT 初始化成功、两声蜂鸣前 | `g_esp8266_init` 仍为 0，`monitor` 尚未投递串口帧 |
| **周期** | `app_task_mqtt`：每 **600 秒**（`ESP8266_NETTIME_SYNC_INTERVAL_S`） | 首次对时已在 esp8266 任务完成，mqtt 里不再重复首次 3s 延迟 |
| **刷卡解锁** | **不调用** | 避免清屏后阻塞 HTTP 5～7s 黑屏；解锁后 `rtc_notify_oled_refresh()` 用 RTC 已有时间刷新 |

周期宏定义：`esp8266_nettime.h` → `#define ESP8266_NETTIME_SYNC_INTERVAL_S 600u`。

---

## 3. GMT 是什么？与北京时间的关系

### 3.1 常见名称

| 名称 | 含义 |
|------|------|
| **GMT** | Greenwich Mean Time，格林威治标准时间（0° 经线） |
| **UTC** | 协调世界时；工程里与 GMT **几乎等同** |
| **北京时间** | 中国标准时间，**UTC/GMT +8**（东八区） |

`www.beijing-time.org` 首页也写明：时区为 **UTC/GMT +8.00**，比 GMT 早 8 小时。

换算关系：

```text
北京时间 = GMT + 8 小时
GMT     = 北京时间 - 8 小时
```

**示例**（与串口日志一致）：

- HTTP 响应头：`Date: Fri, 22 May 2026 19:36:00 GMT` → **GMT 19:36**
- 代码 `apply_cst_offset(+8)` → 北京时间 **5 月 23 日 03:36**（跨日）

因此日志里看到 **19 点 GMT**、OLED 显示 **凌晨 3 点多**，是 **时区换算正确**，不是解析错误。

### 3.2 为什么 HTTP 头里用 GMT？

HTTP 是国际标准（RFC 9110 等）。响应头 **`Date:` 统一用 GMT/UTC**，便于全球服务器与客户端一致解析。  
本工程 **不直接显示 HTTP 字符串**，而是 **GMT+8 后写入 RTC**，再由 `app_task_rtc` 显示北京时间。

---

## 4. HTTP 协议格式与本工程如何请求

### 4.1 HTTP 是什么？

HTTP（HyperText Transfer Protocol）规定 **客户端** 与 **Web 服务器** 如何交换一次「请求 + 响应」：

```text
STM32 --USART3--> ESP8266 --WiFi TCP link 1--> www.beijing-time.org:80
```

- **TCP 80 端口**：承载 HTTP 文本（`esp8266_nettime.c` 中 `NETTIME_PORT 80`）
- **ESP8266**：建立 TCP、收发字节；**STM32**：组 HTTP 请求并解析响应

### 4.2 HTTP 请求（Request）结构

一次请求 = **请求行** + **请求头** + **空行** + **可选请求体**。

```text
┌─────────────────────────────────────────┐
│ 请求行：方法 + 路径 + 协议版本              │
├─────────────────────────────────────────┤
│ 请求头：Host: ... 等（多行 key: value）   │
├─────────────────────────────────────────┤
│ 空行（仅 \r\n，表示头结束）               │
├─────────────────────────────────────────┤
│ 请求体（GET 通常无）                      │
└─────────────────────────────────────────┘
```

本工程实际发送（与温工 demo 一致，`esp8266_nettime.c`）：

```http
GET /time15.asp HTTP/1.1
Host:www.beijing-time.org
...........

```

| 行 | 含义 |
|----|------|
| `GET /time15.asp HTTP/1.1` | **GET** 方法；资源路径 `/time15.asp`；协议 **HTTP/1.1** |
| `Host:www.beijing-time.org` | 虚拟主机名（同一 IP 多站点时必须） |
| 空行 | 头结束 |
| `...........` | demo 中附加一行，促使服务器返回完整响应 |

**GET** 特点：参数多在 URL，**无请求体**；适合「取一个页面/资源」。

**ESP8266 发送顺序**：

1. `AT+CIPSTART=1,"TCP","www.beijing-time.org",80`
2. `AT+CIPSEND=1,长度` → 等待 `>`
3. 将上述 HTTP 文本 **原样** 发出

### 4.3 HTTP 响应（Response）结构

```text
┌─────────────────────────────────────────┐
│ 状态行：HTTP/1.1 200 OK                  │
├─────────────────────────────────────────┤
│ 响应头：Date: Fri, 22 May 2026 19:36:00 GMT
│        Content-Type: text/html; ...
│        Content-Length: ...
│        ...                               │
├─────────────────────────────────────────┤
│ 空行 \r\n                                │
├─────────────────────────────────────────┤
│ 响应体：HTML 源码（可能很长）             │
└─────────────────────────────────────────┘
```

| 部分 | 说明 |
|------|------|
| **状态行** | `HTTP/1.1 200 OK` 表示成功；`404` 未找到等 |
| **响应头 `Date:`** | 服务器时刻，**固定 GMT 格式**；**本工程只解析这一行** |
| **响应体** | 网页 HTML；浏览器用 JS 显示「现在北京时间」，MCU **不解析** |

`Date` 行格式（固定）：

```http
Date: Fri, 22 May 2026 19:36:00 GMT
```

即：`星期, 日 月 年 时:分:秒 GMT` → `sscanf` + `apply_cst_offset(+8)`。

### 4.4 `\r\n` 是什么？

HTTP 规定每行以 **CRLF** 结束：

- `\r`（0x0D）回车 + `\n`（0x0A）换行

代码里：

```c
"GET /time15.asp HTTP/1.1\r\n"
"Host:www.beijing-time.org\r\n"
```

在网络上传的是 **真实 CRLF 字节**，不是字面字符串 `\r\n`。  
**连续两个 `\r\n`（中间无其它头）** = 响应头与 HTML body 的分界。

### 4.5 本工程在 `g_esp8266_rx_buf` 里做什么

```text
1. 发出 HTTP GET（见 4.2）
2. TCP 收到字节写入 g_esp8266_rx_buf，例如：
   HTTP/1.1 200 OK\r\n
   Date: Fri, 22 May 2026 19:36:00 GMT\r\n
   Content-Type: text/html\r\n
   ...
   \r\n
   <!DOCTYPE html>...
3. 等待出现 "GMT" 且接收稳定（防半包）
4. 截取 Date: ... GMT 整行
5. GMT+8 → RTC → rtc_notify_oled_refresh → app_task_rtc → OLED
```

**为何不等 HTML 里的 `<font id="hrs">`？**  
STM32 **不能执行 JavaScript**；解析标准 HTTP 头 **`Date:`** 是嵌入式常用、可靠做法。

---

## 5. 网站首页 HTML 与 `/time15.asp` 的区别

| 项目 | 首页 `/`（浏览器打开的主页） | 本工程请求的 `/time15.asp` |
|------|------------------------------|----------------------------|
| 时间展示 | JS 加载 `time.asp`，填 `#hrs/#min/#sec` | 仍返回完整 HTTP 响应 |
| MCU 可用性 | 需跑 JS，**不适合** | 读响应头 **`Date:`（GMT）** 即可 |
| 时区说明 | 页面写「东八区 GMT+8」 | **`Date` 头仍是 GMT**，需代码 +8 |

温工 demo 与本工程均请求 **`/time15.asp`**，不是首页 HTML。

---

## 6. HTTP 对时实现要点（与温工 demo 的差异）

### 6.1 例程做法（单连接 + 透传）

温工 demo 流程：

1. `AT+CIPSTART="TCP","www.beijing-time.org",80`
2. 进入 **透传模式**（`AT+CIPMODE=1` + `AT+CIPSEND`）
3. 发送 `GET /time15.asp HTTP/1.1` + `Host:...` + `...........\r\n`
4. 在 `g_esp8266_rx_buf` 中找 `"Date"` 行

**不能原样照搬到本工程**：本工程已用 **MQTT AT 指令**（`AT+MQTTCONN` 等）上云，透传/CIPMODE 会与 MQTT 栈冲突。

### 6.2 本工程做法（多连接 + 非透传）

| 项目 | 配置 |
|------|------|
| 多连接 | `esp8266_mqtt_init()` 里 `AT+CIPMUX=1` |
| MQTT | **link 0**：`AT+MQTTUSERCFG=0`、`AT+MQTTCONN=0`、`AT+MQTTPUB=0`… |
| HTTP 校时 | **link 1**：`AT+CIPSTART=1,"TCP","www.beijing-time.org",80` → `AT+CIPSEND=1,len` → `AT+CIPCLOSE=1` |
| HTTP 正文 | 与 demo 相同：`GET /time15.asp` + `Host:www.beijing-time.org` + `...........\r\n` |
| 时区 | HTTP `Date` 为 **GMT**，代码 **`+8` 小时** 得到北京时间再写 RTC |

### 6.3 解析与写 RTC

1. 等待接收中出现 **`GMT`** 且字节计数稳定约 60ms（避免半包）。
2. 从 `"Date:"` 截取到 `"GMT"`（含）。
3. `sscanf` 解析 → `apply_cst_offset(+8)` → `RTC_SetDate` / `RTC_SetTime`（BCD，与 `app_task_rtc` 一致）。
4. 成功日志示例：  
   `esp8266_nettime_sync OK [Date: Fri, 22 May 2026 19:36:00 GMT] -> Beijing 03:36:00`

---

## 7. OLED 显示逻辑（UI 未改）

`app_task_rtc` 仍使用原有坐标与控件类型，仅时间 **来源** 变为网络校准后的 RTC：

| 区域 | 坐标（约） | 内容 |
|------|------------|------|
| 时间 | y=0, x=25 | `%02x:%02x:%02x`（BCD 时:分:秒） |
| 日期 | y=3, x=25 | `20%02x-%02x-%02x` |
| 星期 | y=5 | 汉字 + 星期 BCD |

`rtc_init()` 仅写入占位初值（2000-01-01 00:00:00），联网对时成功后由 RTC 实际走时。

---

## 8. 互斥锁与 link 0 / link 1

### 8.1 link 含义

- **link 0**：ESP8266 **MQTT AT** 的连接槽（`AT+MQTTCONN=0,...`），用于 OneNET。
- **link 1**：**普通 TCP**（`AT+CIPSTART=1,...`），专用于 HTTP 校时，用完 `CIPCLOSE`。

二者在模块内可并行（依赖固件支持 `CIPMUX=1`），但 MCU 侧只有 **一条 USART3**。

### 8.2 `g_mutex_esp8266` 保护什么

保护 **串口 + 接收缓冲** 的整段 AT 交互，而不是 link 编号本身：

- `g_esp8266_rx_buf[512]`、`g_esp8266_rx_cnt`
- 发 AT、等响应、清空/解析缓冲的时序

使用者：`esp8266_nettime_sync()`（全程持锁）、`mqtt_send_heart()`、`mqtt_publish_data()`；`app_task_monitor` 拿不到锁则 **不入队**，避免与对时/MQTT 抢缓冲。

---

## 9. 与 MQTT 是否冲突

**结论**：协议上不必然冲突；实现上通过 **CIPMUX 分 link + 互斥串行化 USART** 共存。

| 对比项 | HTTP 校时 | MQTT 上云 |
|--------|-----------|-----------|
| 应用协议 | HTTP/1.1 over TCP | MQTT over TCP |
| 本工程 AT | `CIPSTART=1` + 发 HTTP 文本 | `MQTTCONN=0` + `MQTTPUB=0` 等 |
| 数据形态 | 请求行 + 头 + HTML body | AT + JSON 主题 |
| 时间来源 | 响应头 **`Date:`（GMT）** | 不负责校时 |

| 风险 | 说明 | 当前缓解 |
|------|------|----------|
| 对时期间云端下发丢失 | HTTP 路径多次 `esp8266_send_at` 会清 rx 缓冲，中途到的 `+MQTTSUBRECV` 可能被清掉 | 结束时若缓冲含 `+MQTTSUBRECV` 则保留；对时持锁时 monitor 不入队 |
| 对时阻塞心跳/上报 | 单次对时约 5～15s，mqtt 循环内先对时再 PING/上报 | 仅每 10 分钟一次；解锁不对时 |
| 固件不支持 CIPMUX | `CIPSTART=1` 失败 | 日志 `CIPSTART fail`，重试 3 次，MQTT 主链路通常仍在 |

---

## 10. 开发过程中遇到的问题与解决

### 10.1 需求：MQTT 与 HTTP 校时并存

**问题**：温工 demo 用透传 + 单 TCP，会直接破坏已有 MQTT 会话。

**解决**：

- WiFi 连上后 `AT+CIPMUX=1`；
- MQTT 固定 **link 0**（`AT+MQTT*` 第一个参数为 0）；
- HTTP 校时走 **link 1** 短连接，非透传 `CIPSEND`；
- 新增 `esp8266_nettime.c`，`esp8266_mqtt_init` 之后调用对时。

---

### 10.2 链接错误 `Undefined symbol ESP8266_SendCmdPolls`

**现象**：Keil 编译 `esp8266_nettime.o` 报 L6218E。

**原因**：`ESP8266_SendCmdPolls` 在 `esp8266_mqtt.c` 中为 `static`，外部不可链接。

**解决**：去掉 `static`，在 `esp8266_mqtt.h` 中 `extern` 声明；后 HTTP 路径改为 `esp8266_send_at` + `esp8266_find_str_in_rx_packet`，避免 HTTP 响应里的 `OK` 被 `SendCmdPolls` 误清缓冲。

---

### 10.3 对时失败 / 解析错误 `Date: Fri, 2`

**现象**：日志 `RTC set fail [Date: Fri, 2]` 或 `no Date line`。

**原因**：

1. 仅匹配 `"Date"` 且接收“稳定”过早，TCP 半包时只有半行；
2. `g_esp8266_rx_buf` 中断写入 **未补 `\0'`**，字符串解析不可靠。

**解决**：

1. 改为等待 **`GMT`** 出现且稳定 ~60ms 再解析；
2. `nettime_rx_terminate()` 在 `g_esp8266_rx_cnt` 处补 `'\0'`；
3. 截取 **Date: … GMT** 整行再 `sscanf`；
4. 发 HTTP 前清空 rx，避免 MQTT 残留干扰。

---

### 10.4 刷卡解锁后黑屏 5～7 秒

**现象**：解锁后 OLED 长时间空白才出主菜单。

**原因**：解锁路径在 `OLED_CTRL_CLEAR` 之后 **同步调用** `esp8266_nettime_sync()`，而 `app_task_rtc` 尚未恢复，无法刷新。

**解决**：

- **删除**解锁路径中的 `esp8266_nettime_sync()`（MQTT 连通时已对时）；
- 清屏后立即 `vTaskResume(app_task_rtc)` + `rtc_notify_oled_refresh()`；
- 正确图标显示由 500ms 改为 200ms。

---

### 10.5 日志 19 点与 OLED 凌晨 3 点不一致

**现象**：  
`Date: ... 19:36:00 GMT` 日志里像“晚上 7 点”，OLED 显示 **03:36**。

**原因**：HTTP `Date` 是 **GMT（UTC）**；代码 **`+8` 得北京时间** → 19:36 + 8h = 次日 03:36，**OLED 正确**。

**说明**：曾尝试去掉 +8 使 OLED 与日志数字一致，但与真实北京时间不符；**最终保留 GMT+8**。成功日志增加 `-> Beijing hh:mm:ss` 便于对照。

---

### 10.6 Keil 编译中文乱码 / 缺引号

**现象**：`main.c(454): missing closing quote`，`invalid multibyte character sequence`。

**原因**：源文件 UTF-8 中文 + 全角标点，Keil 按 GBK 解析导致字符串截断。

**解决**：报错行的 `dgb_printf_safe` 改为 **纯 ASCII** 调试串（功能不变）。若需中文日志，在 Keil 中将文件存为 GB2312/GBK。

---

### 10.7 `rtc_notify_oled_refresh` 解锁时是否还要调用

**作用**：置位 `EVENT_GROUP_RTC_WAKEUP`，让 `app_task_rtc` **立即**从 RTC 刷新 OLED，而不等下一秒 RTC 中断。

**结论**：解锁路径 **建议保留**；与网络对时无直接关系，只负责 **清屏后马上出主界面时间**。

---

## 11. 相关文件一览

| 文件 | 职责 |
|------|------|
| `esp8266_nettime.c` / `.h` | HTTP 对时、GMT+8、写 RTC |
| `esp8266_mqtt.c` | `CIPMUX=1`、MQTT link 0、`esp8266_uart_lock` |
| `rtc.c` | `rtc_init` 占位、`rtc_notify_oled_refresh`、RTC 1Hz 唤醒 |
| `main.c` `app_task_rtc` | 读 RTC → OLED 队列 |
| `main.c` `app_task_esp8266` | MQTT 成功后首次 `esp8266_nettime_sync()` |
| `main.c` `app_task_mqtt` | 每 600s 周期对时 |
| `main.c` `app_task_rfid` | 解锁恢复 RTC，**不** HTTP 对时 |

---

## 12. 调试建议

1. 串口搜 `esp8266_nettime_sync OK` / `fail` / `wait GMT timeout` / `CIPSTART fail`。
2. 成功时应同时看到 **GMT 行** 与 **`-> Beijing`** 时间，且与手机北京时间一致。
3. 若控灯偶发超时，排查对时窗口内是否丢失 `+MQTTSUBRECV`（见第 9 节）。
4. 修改校时间隔：改 `ESP8266_NETTIME_SYNC_INTERVAL_S`（单位：秒）。

---

## 13. 可选后续优化（未实现）

- 对时前若 rx 中已有 `+MQTTSUBRECV`，先入队再 HTTP，降低控灯丢失概率。
- 周期对时放到独立低优先级任务，或放在 `mqtt_report` **之后**，缩短无心跳窗口。
- 增大 `g_esp8266_rx_buf`（若频繁 `rx=511` 且 `wait GMT timeout`）。
