# FreeRTOS 调试打印与 ESP8266 联调问题总结

本文档基于「智慧语音安防」工程中 **USART1 调试日志**、**`dgb_printf_safe`** 与 **ESP8266（USART3）** 联调时遇到的问题整理：说明现象、结合仓库内真实代码分析原因、记录已采用的修改，并列出 **FreeRTOS** 侧需注意的事项。

---

## 1. 文档适用范围与相关文件

| 路径 | 说明 |
|------|------|
| `02源代码(VET6)/USER/main.c` | `dgb_printf_safe`、`app_task_init` 互斥量创建顺序、`app_task_esp8266` |
| `02源代码(VET6)/USER/includes.h` | `g_mutex_printf`、`dgb_printf_safe` 声明 |
| `02源代码(VET6)/HARDWARE/ESP8266/esp8266_mqtt.c` | `ESP8266_SendCmd` / `ESP8266_WaitRecive`、MQTT 初始化日志 |
| `02源代码(VET6)/HARDWARE/ESP8266/esp8266.c` | `esp8266_send_at` 与 RX 缓冲处理 |
| `02源代码(VET6)/SYSTEM/usart/usart.c` | USART1 调试口、`USART3_IRQHandler`、USART3 NVIC 优先级 |
| `02源代码(VET6)/VET6引脚配置.txt` | PA9/PA10（串口1）、PB10/PB11（与 ESP8266 对接的 USART3） |

---

## 2. 典型现象（「看不到 ESP8266 相关日志」）

- 串口助手接 **USART1（调试）** 时，只能看到部分系统日志，**长时间看不到** ESP8266 初始化后续打印，误以为程序卡死或未执行。
- 或 **`dgb_printf_safe` 一经调用整任务再无输出**，与其它任务日志交错异常。

上述现象往往是 **多个因素叠加**：互斥量使用不当、AT 接收逻辑错误、发送后误清空 RX、阻塞时间过长、波特率不一致等。

---

## 3. 硬件与日志通道（必须先分清）

本工程中有 **两条独立的 UART 链路**：

1. **USART1（PA9 TX / PA10 RX）**  
   - 用途：**人机调试**，`printf` / `dgb_printf_safe` → `fputc` → USART1。  
   - 工程里波特率：**9600**（见 `main.c` 中 `uart1_init(9600)`）。  
   - PC 侧 USB‑TTL：**接 PA9（MCU TX）→ TTL RX**，**GND 共地**，串口助手选 **9600 8N1**。

2. **USART3（PB10 / PB11）**  
   - 用途：**STM32 ↔ ESP8266 AT 指令**，与模块电气相连（板上走线或飞线）。  
   - 波特率通常为 **115200**（`esp8266_init(115200)`）。

**注意：** 在 USART1 上 **看不到** ESP8266 模块原生的 UART 打印；你能看到的是 **MCU 代码里主动打印的字符串**。若要监听模块裸 UART，需单独用 USB‑TTL 接模块 TX/RX（与本工程调试口无关）。

---

## 4. 原因分析与对应代码

### 4.1 `xSemaphoreTake(NULL)` 与 `dgb_printf_safe`

**问题：**  
全局变量 `g_mutex_printf` 默认值为 **NULL**（未创建互斥量前）。若在此状态下调用：

```c
xSemaphoreTake(g_mutex_printf, portMAX_DELAY);
```

在 FreeRTOS 中属于 **对无效句柄操作**，行为未定义：可能触发 **`configASSERT`**、HardFault，或表现为 **永久阻塞 / 调度异常**，导致 **`vprintf` 根本执行不到**，串口上就像「完全无法打印」。

**工程中的防护写法（`main.c`）：**

```171:192:02源代码(VET6)/USER/main.c
#define DEBUG_PRINTF_EN	1
void dgb_printf_safe(const char *format, ...)
{
#if DEBUG_PRINTF_EN	

	va_list args;
	va_start(args, format);

	/* 互斥量未创建前避免 xSemaphoreTake(NULL) 死机（仅早期启动阶段） */
	if (g_mutex_printf != NULL)
		xSemaphoreTake(g_mutex_printf, portMAX_DELAY);

	vprintf(format, args);

	if (g_mutex_printf != NULL)
		xSemaphoreGive(g_mutex_printf);

	va_end(args);
#else
	(void)0;
#endif
}
```

**要点：**

- **Take / Give 必须成对**，且只对 **非 NULL** 句柄调用。
- 在句柄为 NULL 时退化为 **不加锁的 `vprintf`**：仍能输出调试信息，但 **不保证多任务下字符不穿插**；因此 **正常运行时仍应保证：任意任务首次调用 `dgb_printf_safe` 之前，`g_mutex_printf` 已完成创建**。

**当前工程中的创建顺序（`app_task_init`）：**

```267:272:02源代码(VET6)/USER/main.c
	printf("[app_task_init] create success\r\n");	
	
	/* 创建互斥型信号量(互斥锁) */	  
	g_mutex_printf=xSemaphoreCreateMutex();	
	g_mutex_card = xSemaphoreCreateMutex();  // 专门保护有效卡ID的互斥锁
	g_mutex_alarm = xSemaphoreCreateMutex();
```

后续在 **同一函数内** 通过 `task_tbl` 循环创建各任务（含 `app_task_esp8266`）。因此在 **当前代码结构下**，ESP8266 任务首次调用 `dgb_printf_safe` 时，互斥量通常已有效；**NULL 判断仍建议保留**，用于防御将来调整初始化顺序、或误在中断/极早期调用时的崩溃。

---

### 4.2 多任务下混用 `printf` 与 `dgb_printf_safe`

**问题：**  
多个任务同时往 USART1 输出时，若一部分走 **`printf`（无互斥）**、一部分走 **`dgb_printf_safe`（有互斥）**，可能出现：

- 同一行被拆开、乱序；
- 在极端情况下与阻塞、缓冲区行为叠加，**误以为「某模块没有日志」**。

**建议：**  
凡是在 **FreeRTOS 任务上下文** 中的调试输出，尽量 **统一使用 `dgb_printf_safe`**（ESP8266/MQTT 相关日志已按此方式修改）。

---

### 4.3 `ESP8266_WaitRecive` 内误清空缓冲区（AT 指令永远匹配失败）

**问题：**  
早期实现在「接收计数不变、认定一帧结束」分支里调用了 **`ESP8266_Clear()`**，会在 **`ESP8266_SendCmd` 执行 `strstr` 之前把 `g_esp8266_rx_buf` 清空**，导致 **永远匹配不到 `"OK"`**，逻辑表现为超时、初始化失败；若此时又没有足够的进度打印，用户只会觉得「没有 ESP8266 日志」。

**正确原则：**  
「帧结束」检测 **只应更新状态**，**不得**在该处清空缓冲；匹配成功或确认无用帧后再清空。

**当前代码（节选，`esp8266_mqtt.c`）：**

```27:39:02源代码(VET6)/HARDWARE/ESP8266/esp8266_mqtt.c
// 等待接收完成（字节数在一段时间内不再增加则认为本帧结束）
// 注意：此处绝不能清空缓冲区，否则 ESP8266_SendCmd 里的 strstr 永远对着空缓冲
unsigned char ESP8266_WaitRecive(void)
{
	if(g_esp8266_rx_cnt == 0)
		return REV_WAIT;

	if(g_esp8266_rx_cnt == g_esp8266_rx_cnt_pre)
		return REV_OK;

	g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
	return REV_WAIT;
}
```

---

### 4.4 `esp8266_send_at`：发送后再清空 RX（丢掉模块返回的 `OK`）

**问题：**  
旧逻辑曾在 **发送 AT 后短暂延时再 `memset` 接收缓冲**。ESP8266 在 115200 下常在 **几毫秒内** 返回 `OK`，数据先入缓冲再被 **一键清空**，后续 `esp8266_find_str_in_rx_packet("OK", …)` 必然超时。

**当前代码（节选，`esp8266.c`）：**

```26:34:02源代码(VET6)/HARDWARE/ESP8266/esp8266.c
void esp8266_send_at(char *str)
{
	/* 必须在发送前清空：先发再等 10ms 再清空会把模块已返回的 OK 删掉，
	 * 导致 esp8266_reset / self_test / connect_ap 等全部误判超时 */
	memset((void *)g_esp8266_rx_buf, 0, sizeof g_esp8266_rx_buf);
	g_esp8266_rx_cnt = 0;

	usart3_send_str(str);
}
```

---

### 4.5 长超时与「看起来像没日志」

**问题：**  
`ESP8266_SendCmd` 内部一轮最多约 **800 × 10 ms ≈ 8 s**；若中间再无 `dgb_printf_safe`，串口上会长时间无新行，容易被误判为「断日志」。

**处理：**  
为初始化阶段增加 **`ESP8266_SendCmdPolls(cmd, res, polls)`**，缩短 AT 探测轮询间隔，并在循环中打印 **`esp8266 AT try n`** 等进度（见 `esp8266_mqtt.c` 中 `esp8266_mqtt_init`）。

---

### 4.6 任务栈与大数组（`app_task_esp8266`）

**问题：**  
`esp8266_mqtt_init` 内若使用较大的局部缓冲区（如 **512 字节**），叠加调用栈，在 **`app_task_esp8266` 栈深度较小** 时可能发生 **栈溢出**，表现为随机 HardFault、任务无声退出。

**处理：**

- 将长命令缓冲改为 **文件内静态缓冲**（如 `s_esp8266_cmd_buf[512]`）。
- 适当增大 **`app_task_esp8266` 的栈配置**（当前任务表中为 **1536**，单位为 FreeRTOS 端口定义的栈深度，见 `main.c` 中 `task_tbl`）。

---

### 4.7 USART3 中断优先级与 FreeRTOS（Cortex‑M）

**问题：**  
USART3 接收中断服务程序中使用了 **`taskENTER_CRITICAL_FROM_ISR` / `taskEXIT_CRITICAL_FROM_ISR`**。在 Cortex‑M + FreeRTOS 下，**优先级数值过小（过高优先级）** 的中断若违反内核约定，可能带来不稳定行为。

**当前做法（`usart.c`）：**  
将 USART3 抢占优先级设为与工程约定一致的 **`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`**，与 USART2 等行为对齐。

```216:222:02源代码(VET6)/SYSTEM/usart/usart.c
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	/* 与 USART2 一致：优先级不得低于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，
	 * 否则 ISR 内调用 taskENTER_CRITICAL_FROM_ISR 不符合 FreeRTOS Cortex-M 约定 */
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
```

具体阈值以 **`FreeRTOSConfig.h`** 中 **`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`** 为准。

---

## 5. 解决方法汇总表

| 序号 | 问题 | 处理思路 |
|------|------|----------|
| 1 | `xSemaphoreTake(NULL)` 导致异常或无法打印 | `dgb_printf_safe` 内 **先判断 `g_mutex_printf != NULL`** 再 Take/Give |
| 2 | 任务间打印交错 / 不可靠 | 任务内调试输出优先 **`dgb_printf_safe`** |
| 3 | `ESP8266_WaitRecive` 提前清空缓冲 | **帧检测分支不清空**，仅在 SendCmd 匹配成功或丢弃无效帧时清空 |
| 4 | `esp8266_send_at` 清空掉已收到的 `OK` | **仅在发送前清空 RX** |
| 5 | AT 等待过久、无进度感 | **`ESP8266_SendCmdPolls` + 周期性 `dgb_printf_safe` 进度行** |
| 6 | 栈溢出风险 | **静态 `s_esp8266_cmd_buf`** + **增大 `app_task_esp8266` 栈** |
| 7 | USART3 与 RTOS 中断约定 | **NVIC 优先级按 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 配置** |
| 8 | 串口助手波特率错误 | 调试口 USART1 为 **9600**（与 `uart1_init` 一致） |

---

## 6. FreeRTOS 使用注意事项（与本工程强相关）

1. **信号量 / 互斥量句柄**  
   - 创建前为 NULL；**禁止**对 NULL 执行 `xSemaphoreTake` / `xSemaphoreGive`（除非内核文档明确允许的特例，标准互斥量不适用）。  
   - 创建后建议在 **调度器运行、业务任务启动前** 完成（本工程在 `app_task_init` 早期创建）。

2. **初始化顺序**  
   - 任何使用 `g_mutex_printf` 的路径（含 `dgb_printf_safe`），应保证 **`xSemaphoreCreateMutex` 已返回有效句柄**。  
   - 若将来把「创建任务」移到「创建互斥量」之前，必须同步调整或保留 NULL 防护。

3. **`delay_ms` 与节拍**  
   - 本工程 `delay_ms` 多为 **`vTaskDelay`**；当 **`configTICK_RATE_HZ == 1000`** 时，参数与毫秒在数值上常一致，但若修改节拍频率，需改用 **`pdMS_TO_TICKS`** 换算，避免延时偏差。

4. **中断里慎用打印与 RTOS API**  
   - **避免在 ISR 里调用 `printf` / `vprintf` / 可能阻塞的 API**。  
   - 若 ISR 必须通知任务打印，使用 **队列、任务通知** 等由任务侧输出。

5. **临界区与中断优先级**  
   - 在 ISR 中使用带 **FromISR** 后缀的 API 或临界区时，遵守 **`configMAX_SYSCALL_INTERRUPT_PRIORITY`**（或工程中的 **`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`**）约束。  
   - 不要随意把常用通信中断设为最高抢占优先级，除非清楚其与内核的兼容性。

6. **栈深度**  
   - `xTaskCreate` 的栈参数单位以 **端口说明为准**（STM32 常见为「字」）；含 **sprintf 大缓冲、MQTT 长字符串** 的任务要适当加大栈或使用静态/全局缓冲。

---

## 7. 推荐的调试流程（简要）

1. **确认 USART1**：9600，PA9→TTL RX，GND 共地。  
2. **确认 USART3**：模块接线、115200、共地；与 ESP8266 通信用，不与调试 TTL 混接 expecting MCU printf。  
3. 观察 **`esp8266_exit_transparent_transmission success`** 之后的 **`esp8266 AT try n`** 是否递增：  
   - 若 **长期失败**：重点查 **USART3 硬件与中断**；  
   - 若 **能通过 AT sync**：再查 WiFi / MQTT 配置与 OneNET 三元组。

---

## 8. 修订记录说明

本文档描述的是截至编写时仓库中 **已落地的代码行为**；若后续修改了 `uart1_init` 波特率、`app_task_init` 顺序或 `dgb_printf_safe` 实现，请以实际源码为准并同步更新本节相关段落。

---

*文档名称：`FreeRTOS调试打印与ESP8266联调问题总结.md`*  
*适用工程：智慧语音安防 `02源代码(VET6)`*
