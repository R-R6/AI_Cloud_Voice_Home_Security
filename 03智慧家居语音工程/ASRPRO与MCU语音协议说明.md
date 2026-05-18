# ASRPRO 与 STM32 语音协议说明

> 本文档总结「主动报警」与「被动语音查询」两条链路的实现、验证方法与修改要点。  
> 对应工程：`03智慧家居语音工程/asrpro_project.c`（天问 ASRPRO）+ `02源代码(VET6)/HARDWARE/ASPRO/`（STM32 驱动）+ `02源代码(VET6)/USER/main.c`（FreeRTOS 任务）。

---

## 1. 相关文件

| 路径 | 作用 |
|------|------|
| `03智慧家居语音工程/asrpro_project.c` | 天问语音模块固件（`task_serial`、`ASR_CODE`） |
| `03智慧家居语音工程/智慧管家.png` | 语音命令/流程参考图 |
| `02源代码(VET6)/HARDWARE/ASPRO/aspro.h` | 协议常量与 API 声明 |
| `02源代码(VET6)/HARDWARE/ASPRO/aspro.c` | USART2 下发 `3#`~`6#` 等 |
| `02源代码(VET6)/USER/main.c` | `app_task_asr_alarm`、`app_task_aspro`、传感器信号量 |

**重要：** 修改 `asrpro_project.c` 后必须在天问 IDE 中重新编译并下载到 ASRPRO；仅改 MCU 无法生效主动报警补丁。

---

## 2. 硬件与串口

| 项目 | 说明 |
|------|------|
| 接口 | STM32 **USART2**（PA2=TX→模块 RX，PA3=RX←模块 TX） |
| 波特率 | **9600**（`asr_init(9600)` 与 `Serial2.begin(9600)` 须一致） |
| 报文格式 | ASCII 字符串，以 **`#`** 结尾；模块侧 `readStringUntil('#')` |
| 状态脚 | 模块 PA3 → STM32 PA8（模块工作中为低电平） |

---

## 3. 下行数字协议（MCU → ASRPRO）

与 `aspro.h` / `task_serial()` 中 `FIRE_GET`、`GAS_GET` 分支一致：

| MCU 发送 | 含义 | 模块播报（playid 示例） |
|----------|------|-------------------------|
| `3#` | 火警 **安全** | 10521 当前火警状态 + 10522 安全 |
| `4#` | 火警 **危险** | 10521 + 10523 不安全 |
| `5#` | 烟雾 **安全** | 10524 当前烟雾报警状态 + 10525 安全 |
| `6#` | 烟雾 **危险** | 10524 + 10526 不安全 |
| `25#`、`70#` 等 | 温湿度 **整数** | 须在模块已进入 TEMP/HUMI 分支后发送 |
| `1#`、`2#` | LED 开/关反馈 | 依赖 `asr_id` 为 LED_ON/OFF |

API 封装（`aspro.c`）：

- `asr_notify_fire(danger)`：`danger=0` → `3#`，非 0 → `4#`
- `asr_notify_gas(danger)`：`danger=0` → `5#`，非 0 → `6#`
- `asr_play_temp(temp_int)` / `asr_play_humi(humi_int)`：只发数值，如 `25#`

调试：MCU 串口可见 `[asr_tx] 4#` 等（`asr_send_str` 内打印）。

---

## 4. 上行命令协议（ASRPRO → MCU）

用户说「智慧管家」唤醒后，识别成功在 `ASR_CODE()` 中通过 **Serial2** 发字符串给 MCU（`app_task_aspro` 用 `strstr` 匹配）：

| snid | 用户说法（示例） | 模块发送 | MCU 处理 |
|------|------------------|----------|----------|
| 1 | 打开灯光 | `LED ON#` | 控灯 + `1#` 反馈 |
| 2 | 关闭灯光 | `LED OFF#` | 控灯 + `2#` |
| 3 | 当前温度 | `TEMP#` | `asr_play_temp()` |
| 4 | 当前湿度 | `HUMI#` | `asr_play_humi()` |
| 5 | 当前时间 | `TIME#` | RTC 打包发 `#` |
| 6 | 当前日期 | `DATE#` | RTC 打包发 `#` |
| 7 | 当前火警状态 | `FIRE#` | `asr_notify_fire(g_fire_status)` |
| 8 | 当前烟雾报警状态 | `GAS#` | `asr_notify_gas(g_mq2_status)` |

**被动查询火/烟的本质：** 模块问 MCU → MCU 回 `3#`~`6#` → 模块 `task_serial` 播报（与主动报警共用播报路径）。

---

## 5. 主动报警链路

```
app_task_lm393 / app_task_mq2（始终运行，不挂起）
  → 火焰/烟雾状态沿变化
  → 更新 g_fire_status / g_mq2_status
  → xSemaphoreGive(g_sem_fire_asr / g_sem_mq2_asr)

app_task_asr_alarm（优先级 6，刷卡解锁后 Resume）
  → xSemaphoreTake 成功
  → asr_notify_fire() / asr_notify_gas()
  → USART2 发 "4#" 或 "6#"（危险时）

ASRPRO task_serial()
  → value_u32 = 3/4/5/6
  → 【关键补丁】强制 asr_id = FIRE_GET / GAS_GET（见下文第 7 节）
  → switch 播报完整报警句
```

**设计要点：**

- 报警播报由独立任务 `app_task_asr_alarm` 处理，**不再**在 `app_task_aspro` 里抢信号量，避免与 `TEMP#`/`HUMI#` 同轮处理导致误播。
- 传感器任务未挂起；语音任务刷卡前挂起，期间信号量可积压，解锁后可能连续播报（含恢复为「安全」时）。

主动报警在 MCU 侧依赖 **二值信号量 + 全局状态变量** 解耦传感器与语音任务，详见下文第 9 节。

---

## 6. 被动查询链路

### 6.1 火警 / 烟雾

```
用户：「智慧管家，当前火警状态」
  → ASR_CODE：snid=7，asr_id=7，Serial2 发 "FIRE#"
  → app_task_aspro 匹配 FIRE
  → asr_notify_fire(g_fire_status) → "3#" 或 "4#"
  → task_serial 与主动报警相同 → 播报
```

烟雾：`snid=8` → `GAS#` → `asr_notify_gas()` → `5#` / `6#`。

### 6.2 温度 / 湿度

```
用户：「当前湿度」
  → ASR_CODE：snid=4，asr_id=4（HUMI_GET），发 "HUMI#"
  → MCU asr_play_humi(70) → "70#"
  → task_serial：70 不是 3~6，不修改 asr_id，仍为 HUMI_GET → 播湿度
```

温度同理：`snid=3` → `TEMP#` → 数值 → `TEMP_GET` 分支。

---

## 7. ASRPRO 关键补丁（主动报警必含）

**问题背景：** 旧逻辑若只发 `4#`/`6#` 而 `asr_id` 仍为上次的 `HUMI_GET`，会播成「当前湿度 6」等错误内容。

**解决：** 在 `task_serial()` 解析 `value_u32` 后、`switch(asr_id)` 前增加（约 55–67 行）：

```c
/*
 * MCU 主动报警（无语音识别前置）：
 * 仅发 3#/4#/5#/6#，此处强制进入火警/烟雾分支并播报。
 */
if ((value_u32 & 0x80000000) == 0) {
  if (value_u32 == 3 || value_u32 == 4)
    asr_id = ASR_ID_FIRE_GET;
  else if (value_u32 == 5 || value_u32 == 6)
    asr_id = ASR_ID_GAS_GET;
}
```

与 `case ASR_ID_FIRE_GET` / `ASR_ID_GAS_GET` 中 `value_u32 == 3/4/5/6` 判断一致。

**高字 bit 打包协议：** `value_u32 & 0x80000000` 为 1 时，用高位设置 `asr_id` 并截取低 24 位为数值（LED/温湿度/时间等打包下发），与纯数字 `3#`~`6#` 分支互不冲突。

---

## 8. MCU 任务分工

| 任务 | 优先级 | 挂起时机 | 职责 |
|------|--------|----------|------|
| `app_task_lm393` | — | 否 | 火焰采样，状态变化 `Give(g_sem_fire_asr)` |
| `app_task_mq2` | — | 否 | 烟雾采样，状态变化 `Give(g_sem_mq2_asr)` |
| `app_task_asr_alarm` | 6 | 上电默认挂起；刷卡 Resume；锁屏 Suspend | 仅 Take 报警信号量 → `asr_notify_*` |
| `app_task_aspro` | 5 | 同上 | 处理 `g_usart2_rx_buf`：LED/TEMP/HUMI/TIME/DATE/**FIRE/GAS** |

全局状态：`g_fire_status`、`g_mq2_status`（0=安全，1=危险），由 `g_mutex_alarm` 保护。

---

## 9. 二值信号量设计（主动报警通知）

本节说明 `main.c` 中 `g_sem_fire_asr`、`g_sem_mq2_asr` 与 `app_task_asr_alarm` 的配合原理，便于答辩说明或后续改实时策略。

### 9.1 涉及的对象

| 对象 | 类型 | 作用 |
|------|------|------|
| `g_sem_fire_asr` | 二值信号量 | 火焰状态**沿变化**时，通知语音任务「该读一次火警状态」 |
| `g_sem_mq2_asr` | 二值信号量 | 烟雾状态沿变化时，同上 |
| `g_mutex_alarm` | 互斥锁 | 保护 `g_fire_status`、`g_mq2_status` 读写一致性 |
| `g_fire_status` / `g_mq2_status` | 全局变量 | 保存**当前**安全/危险（0=安全，1=危险）；语音播报读此值 |
| `app_task_asr_alarm` | FreeRTOS 任务 | `Take` 信号量 → 读全局 → `asr_notify_*` |

创建位置（`main.c` 约 306–310 行）：

```c
g_sem_fire_asr = xSemaphoreCreateBinary();  // 初始计数 0，表示「暂无事件」
g_sem_mq2_asr  = xSemaphoreCreateBinary();
```

### 9.2 数据流（生产者 / 消费者）

```
【生产者】app_task_lm393 / app_task_mq2（500ms 采样，不挂起）
  检测到 current_status != last_status（状态沿）
    → Take(g_mutex_alarm) → 写 g_fire_status / g_mq2_status → Give(g_mutex_alarm)
    → xSemaphoreGive(g_sem_*_asr)     // 只敲门，不传数值

【消费者】app_task_asr_alarm（优先级 6，刷卡后 Resume）
  for(;;) {
    fire_triggered = xSemaphoreTake(g_sem_fire_asr, 50ms);
    mq2_triggered  = xSemaphoreTake(g_sem_mq2_asr,  50ms);
    if (fire_triggered) { 读 g_fire_status → asr_notify_fire(); }
    if (mq2_triggered)  { 读 g_mq2_status  → asr_notify_gas();  }
  }
```

**分工原则：**

- **信号量**：只表示「发生过一次状态变化，请处理」——类似门铃，不携带具体数值。
- **全局变量**：表示「当前到底是安全还是危险」——语音发 `3#`/`4#`/`5#`/`6#` 的依据。
- **互斥锁**：保证传感器写全局、语音读全局时不会读到半更新的数据。

### 9.3 为什么用二值信号量能「及时」通知

1. **事件驱动，而非轮询全局变量**  
   状态一变就 `Give`，语音任务若在 `Take` 上阻塞会**立即**被唤醒，不必等传感器下一个 500ms 周期，也不必在语音任务里空转读 `g_fire_status`。

2. **额外延迟的上界**  
   - 调度延迟：通常几毫秒级。  
   - `Take` 使用 `pdMS_TO_TICKS(50)`：若刚好错过一次 `Give`，最坏约多等 **50ms** 才进入下一轮 `Take`。  
   相对 500ms 采样周期，主动播报仍足够快（软实时，非硬实时）。

3. **与采样周期无关的「通知」**  
   检测沿变化后立即 `Give`；语音任务被唤醒后再读**已写好**的全局变量，因此不存在「信号量到了但状态还没更新」的问题（先写全局，再 `Give`）。

4. **独立任务 + 较高优先级**  
   `app_task_asr_alarm`（6）高于 `app_task_aspro`（5），报警发串口不易被 `TEMP#`/`HUMI#` 解析拖住；这也是从 `app_task_aspro` 拆出报警任务的原因之一。

### 9.4 二值信号量的语义与局限

FreeRTOS 二值信号量计数只有 **0 或 1**：

| 操作 | 效果 |
|------|------|
| `Give`（当前为 0） | 变为 1，可唤醒一个正在 `Take` 的任务 |
| `Give`（当前已为 1） | 仍保持为 1，**不会累计多次**（多次变化在未被 `Take` 前可能合并成「一次门铃」） |
| `Take` 成功 | 变为 0 |

**实际影响：** 若在语音任务 `Take` 之前，烟雾快速 `danger → safe` 并连续两次 `Give`，可能只触发**一次**播报，读到的多为**最后一次** `g_mq2_status`（例如只播「安全」）。  
当前工程传感器 500ms 沿检测 + 播报间隔较长时，日志里常见 danger/safe **各播一次**（实测正常）。若将来需要**每一次沿都必播**，可改为：

- 用 **消息队列**（每次沿 `xQueueSend` 一个 `uint8_t danger`），或  
- `Take` 成功后用循环清空队列/多次 `Take` 并只播最新（需改设计）。

### 9.5 不用二值信号量会怎样

| 替代方案 | 对实时性的影响 | 说明 |
|----------|----------------|------|
| 语音任务**轮询** `g_fire_status` | 较差 | 延迟取决于轮询周期；浪费 CPU |
| 传感器任务内直接 `asr_notify_*` | 看似最快 | 串口发送拖慢采样任务；模块耦合重；优先级难管 |
| **任务通知** `xTaskNotifyGive` | 类似二值量 | 更轻量，语义同为「唤醒一次」 |
| **队列**传递 danger 字节 | 可保留多次变化 | 适合快速抖动、每次都要播的场景 |

对本项目「状态变化 → 播一次当前结果」，**二值信号量 + 全局状态**是常见且够用的做法；真正限制响应速度的是 **500ms 采样** 与 **刷卡前任务挂起**，而非二值量本身。

### 9.6 刷卡挂起与信号量积压

- 上电后 `vTaskSuspend(app_task_asr_alarm)`，锁屏时再次挂起。  
- 挂起期间传感器仍可 `Give`，二值量可保持为 1。  
- 刷卡 `vTaskResume` 后，第一次 `Take` 往往立刻成功，读当前 `g_*_status` 补播（可能包含锁屏期间最后一次变化）。  
- 主动报警**不要求**锁屏也播时，需改挂起策略（见第 13 节修改注意点）。

### 9.7 与蓝牙侧信号量的关系

火焰/烟雾变化时同时 `Give`：

- `g_sem_fire_asr` / `g_sem_mq2_asr` → 语音任务  
- `g_sem_fire_bt` / `g_sem_mq2_bt` → 蓝牙任务  

两路互不影响，共用 `g_mutex_alarm` 与全局状态，属于**同一事件、多消费者**模型。

---

## 10. 功能自检清单

| 步骤 | 操作 | 预期 |
|------|------|------|
| 1 | 上电，**RFID 刷卡**进入系统 | `app_task_aspro`、`app_task_asr_alarm` 被 Resume |
| 2 | 触发火焰/烟雾报警 | 调试口 `[asr_tx] 4#` 或 `6#`；喇叭「当前火警/烟雾…不安全」 |
| 3 | 说「智慧管家，当前火警状态」 | 先见模块发 `FIRE#`，再 `[asr_tx] 3#` 或 `4#`，内容与 `g_fire_status` 一致 |
| 4 | 说「当前湿度」 | `HUMI#` → `[asr_tx] xx#`，播湿度而非烟雾 |
| 5 | 天问串口调试 | `val_int`、`asr_id` 在收到 `6#` 时应进入 GAS_GET |

**刷卡前不播报：** 语音相关任务挂起是设计行为，不是传感器故障。

---

## 11. 已知边界与风险

| 问题 | 说明 |
|------|------|
| 温湿度与报警码冲突 | 温度整数 **3、4** 或湿度 **5、6** 时，发 `3#`~`6#` 会被补丁当成火警/烟雾误播 |
| 锁屏期间报警 | 传感器仍 `Give` 信号量，解锁后可能连续补播（见第 9.6 节） |
| 快速连续沿变化 | 二值信号量不累计次数，可能只播最后一次状态（见第 9.4 节） |
| 仅改 MCU | 天问未烧录 55–67 行补丁时，主动报警仍可能播错 |
| 波特率不一致 | 非 9600 时双方均无法正确解析 |
| LED `1#`/`2#` | 依赖 `asr_id` 或打包协议，直发可能不稳定 |

---

## 12. 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 有 `[asr_tx] 6#` 但播「湿度 6」 | 天问固件无 55–67 行 | 重新导出并下载 `asrpro_project.c` |
| 报警完全无声 | 未刷卡 / 任务仍 Suspend | 刷卡解锁 |
| 无 `[asr_tx]` | USART2 接线或波特率 | 查 PA2/PA3、9600 |
| 语音问火警不对 | `g_fire_status` 未更新或传感器逻辑 | 查 `app_task_lm393`、互斥锁 |
| 报警后误播上次温度 | `app_task_aspro` 同轮处理旧 RX | 确认报警已迁到 `app_task_asr_alarm` |

---

## 13. 修改代码时的注意点

1. **改协议数字 3~6：** 须同时改 `aspro.h`、`asrpro_project.c` 的补丁与 `FIRE_GET`/`GAS_GET` 分支。
2. **新增语音命令：** 在天问增加 snid → `ASR_CODE` 发新字符串 → `main.c` 的 `app_task_aspro` 增加 `strstr` 分支。
3. **主动报警内容：** 改 `play_audio(10521)` 等 playid 或天问语音资源，不必改 MCU 数字含义。
4. **锁屏也要报警：** 需调整 `vTaskSuspend(app_task_asr_alarm_handle)` 策略（当前与 `app_task_aspro` 一起挂起）。
5. **恢复安全是否播报：** 当前状态沿变化即 `Give`，危险→安全也会触发 `3#`/`5#`；若不要播安全，需在传感器或 `app_task_asr_alarm` 侧过滤。
6. **改报警实时性：** 优先评估采样周期（500ms）与 `Take` 超时（50ms）；若需保留每次沿，考虑队列替代二值信号量（见第 9.4、9.5 节）。

---

## 14. 链路总览（速查）

```
                    ┌─────────────────────────────────────┐
                    │         主动报警（传感器）            │
                    └─────────────────────────────────────┘
  lm393/mq2 ──Give(二值信号量)──? app_task_asr_alarm ──? asr_notify_* ──? 3#~6#
                                      │
                    ┌─────────────────┴───────────────────┐
                    │         被动查询（用户说话）          │
                    └─────────────────────────────────────┘
  ASR_CODE ──FIRE#/GAS#──? app_task_aspro ──? asr_notify_* ──? 3#~6#
  ASR_CODE ──TEMP#/HUMI#─? app_task_aspro ──? asr_play_* ───? 数值#

  共同出口：ASRPRO task_serial →（补丁设 asr_id）→ switch → play_audio
```

---

*文档版本：与当前仓库 `asrpro_project.c`（含 55–67 行补丁）、`aspro.c` / `main.c`（含二值信号量主动报警）一致。验证或改协议时请同步更新本文档。*
