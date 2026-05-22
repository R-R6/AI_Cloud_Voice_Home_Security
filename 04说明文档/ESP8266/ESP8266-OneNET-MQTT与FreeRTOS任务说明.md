# ESP8266 连接 OneNET（MQTT）与 FreeRTOS 任务说明

本文档对应工程中 `esp8266.c` / `esp8266.h`、`esp8266_mqtt.c` / `esp8266_mqtt.h` 以及 `main.c` 内与 MQTT 相关的任务整理说明，并与源码中新补充的注释一致，便于后续维护与答辩说明。

---

## 1. 总体架构

- **通信方式**：STM32 通过 **USART3** 与 ESP8266 交互；ESP8266 固件使用 **MQTT AT 指令集**（如 `AT+MQTTUSERCFG`、`AT+MQTTCONN`、`AT+MQTTPUB` / `AT+MQTTPUBRAW` 等），在模块内部完成与云端的 MQTT 连接。
- **云平台**：`esp8266_mqtt.h` 中配置为 **OneNET 新版物模型**（域名 `mqtts.heclouds.com`、端口 `1883`，主题形如 `$sys/<产品ID>/<设备名>/thing/property/...`）。
- **MCU 职责**：组网上下文（SSID/密码）、鉴权参数、属性 JSON 上报、解析云端下发的属性设置；不负责 TCP/IP 协议栈实现。

---

## 2. 源文件分工

| 文件 | 作用 |
|------|------|
| `esp8266.h` / `esp8266.c` | USART3 初始化、字节/字符串发送、通用 AT（透传、TCP 等）及 `g_esp8266_rx_buf` 配合逻辑说明。 |
| `esp8266_mqtt.h` | OneNET 产品/设备/鉴权、发布与订阅主题宏，以及对外的 MQTT AT 封装声明。 |
| `esp8266_mqtt.c` | AT 轮询等待、MQTT 连接/订阅/发布（含 JSON 转义与 PUBRAW 长包）、`esp8266_mqtt_init` 全流程、`mqtt_report_devices_status` 组包上报。 |
| `main.c` | 三个与无线相关的 FreeRTOS 任务：`app_task_mqtt`、`app_task_esp8266`、`app_task_monitor`，以及队列 `g_queue_esp8266`、标志 `g_esp8266_init`。 |

---

## 3. `main.c` 中 MQTT 相关三任务协作

### 3.1 `app_task_esp8266`（栈 1536，优先级 5）

- 循环调用 **`esp8266_mqtt_init()`** 直至成功，完成：退出透传、AT 同步、STA、DHCP、`CWJAP`、`MQTTCLEAN`、`MQTTUSERCFG`、`MQTTCONN`、订阅 `property/set` 与可选 `post/reply`。
- 成功后蜂鸣提示，执行 **`vTaskResume(g_app_task_mqtt_handle)`**，再置 **`g_esp8266_init = 1`**。
- 之后阻塞在 **`xQueueReceive(g_queue_esp8266, …, portMAX_DELAY)`**，从队列取串口帧，调用 **`mqtt_handle_property_set()`** 解析 `property/set` 并控灯、发 `set_reply`（详见 [OneNET云端下行控灯与set_reply应答说明.md](./OneNET云端下行控灯与set_reply应答说明.md)）。

### 3.2 `app_task_mqtt`（栈 512，优先级 5）

- 创建后打印日志，随即 **`vTaskSuspend(NULL)`** 自挂起，**避免在 WiFi/MQTT 未就绪时发送 AT**。
- 被 `app_task_esp8266` 恢复后，循环：**心跳 `mqtt_send_heart()`**、**属性上报 `mqtt_report_devices_status()`**，并定时从 **`g_queue_dht11`** 短时接收温湿度以更新 **`g_temp` / `g_humi`**（火焰/烟雾仅读全局变量，避免错误覆盖）。

### 3.3 `app_task_monitor`（栈 1024，优先级 5）

- 周期性采样 **`g_esp8266_rx_cnt`**，若与约 10ms 前相等且 **`g_esp8266_init`** 已为 1，则认为 **一帧接收结束**，将 **`g_esp8266_rx_buf`** 拷贝入 **`g_queue_esp8266`** 并清空接收缓冲。
- 将 **“接收完成判断”** 与 **“AT 长流程 + JSON 解析”** 分到不同任务，减轻 USART 中断与业务逻辑耦合，并避免在 ISR 中做繁重处理（具体入中断的路径以 `usart.c` 为准）。

### 3.4 队列 `g_queue_esp8266`

- **深度 3**，每个元素长度 **`sizeof(g_esp8266_rx_buf)`（512 字节）**。
- **生产者**：`app_task_monitor`；**消费者**：`app_task_esp8266`。

---

## 4. FreeRTOS 调度与优先级（结合本工程）

### 4.1 优先级配置

- 在 `main.c` 的 **`task_tbl`** 中，包括 **`app_task_mqtt`、`app_task_esp8266`、`app_task_monitor`** 在内的多项任务均配置为 **同一优先级 5**（与 `app_task_init` 中 `xTaskCreate` 的用法一致）。
- **调度规则（概念）**：FreeRTOS 在 **可抢占** 前提下，总是运行 **当前就绪任务中优先级最高** 的任务；若 **多个任务优先级相同**，常见配置下会在每个 **tick** 间 **轮转**（时间片），避免同优先级任务之一长期占用 CPU（具体依赖 `FreeRTOSConfig.h` 中 **`configUSE_TIME_SLICING`** 等选项）。

### 4.2 本工程中的“让出 CPU”方式

即使优先级相同，任务仍会在以下位置 **阻塞或延时**，从而触发调度器切换到其它就绪任务：

- **`vTaskDelay` / `vTaskDelayUntil`**：例如 MQTT AT 流程、`app_task_monitor` 中的 `delay_ms(10)` 等（内部会转换为 tick 延时）。
- **`vTaskSuspend`**：`app_task_mqtt` 在连接完成前挂起自身。
- **队列、信号量**：如 **`xQueueReceive(..., portMAX_DELAY)`**、`xQueueSend` 带超时；`app_task_esp8266` 在队列为空时阻塞等待，不空转占用 CPU。

### 4.3 同优先级下的设计含义

- **MQTT 初始化**（`app_task_esp8266`）与 **周期上报**（`app_task_mqtt`）串行化依赖 **`vTaskSuspend` / `vTaskResume`**，而不是靠更高优先级“抢跑”，逻辑清晰：**先连上云，再周期上报**。
- 初始化阶段 **`esp8266_mqtt_init` 内部大量 AT 等待** 期间，其它 **同级优先级** 任务仍可按 tick 轮转运行；若某段代码在 **临界区关中断** 或 **长时间无延时**，则可能 **饿死** 同级任务，属嵌入式常见隐患，扩展功能时需注意 **AT 轮询内已有 `vTaskDelay`** 的设计。

### 4.4 栈空间为何不同

- **`app_task_esp8266`** 栈 **1536**：内部调用 **`esp8266_mqtt_init`**，包含多层 AT、较长 `sprintf` 缓冲及错误路径打印，栈需求大于普通传感器任务。
- **`app_task_mqtt`** 栈 **512**：循环内主要是 MQTT API 与短逻辑。
- **`app_task_monitor`** 栈 **1024**：队列发送、局部变量与日志；略高于 512 以留余量。

---

## 5. 数据流简图（逻辑）

```text
[ESP8266] <--UART3--> [MCU: 中断写 g_esp8266_rx_buf / g_esp8266_rx_cnt]
                              |
                              v
              app_task_monitor（帧结束检测，g_esp8266_init==1）
                              |
                     xQueueSend(g_queue_esp8266)
                              |
                              v
              app_task_esp8266（解析下行，控制 GPIO）
```

上行属性：`app_task_mqtt` → `mqtt_report_devices_status` → `mqtt_publish_data` → USART3 AT → ESP8266 → OneNET。

**上行发布细节**（`topic` 从哪来、`snprintf` 的 `n`、缓冲区 1200 与 AT 行 230 字节、`mqtt_publish_data` 逐句逻辑）见：  
[ESP8266-MQTT属性上报与mqtt_publish_data说明.md](./ESP8266-MQTT属性上报与mqtt_publish_data说明.md)。

**下行控灯与 set_reply** 见：  
[OneNET云端下行控灯与set_reply应答说明.md](./OneNET云端下行控灯与set_reply应答说明.md)。

---

## 6. 维护与联调注意点

- **硬件接线**：注释中强调 ESP8266 接 **PB10/PB11（USART3）**，勿与 UART4 等混淆；无 RX 时常表现为 AT 无响应（`esp8266_mqtt.c` 中有调试打印辅助判断）。
- **云端物模型**：`mqtt_report_devices_status` 中属性标识需与 OneNET 控制台 **物模型标识符** 一致，否则平台可能丢弃或仅记录。
- **敏感信息**：`esp8266_mqtt.h` 中含 **产品 ID、设备名、鉴权 token**，版本库外发前请脱敏。

---

## 7. 文档与代码的关系

- 源码中仅 **增加注释**（含对原误标任务说明的纠正性注释），**未改变** 控制逻辑、宏取值与 API 行为。
- 若后续调整任务优先级，请同时对照 **`FreeRTOSConfig.h`** 中的 **`configMAX_PRIORITIES`**、**中断优先级分组**（本工程 `main` 中使用 **`NVIC_PriorityGroup_4`**）及 **SysTick 作为 OS 心跳** 的配置，避免 **优先级反转** 或 **中断嵌套与 FreeRTOS API 调用规则** 冲突。

---

*文档生成日期：2026-05-11，与工程目录 `04说明文档` 下其它说明并列存档。*
