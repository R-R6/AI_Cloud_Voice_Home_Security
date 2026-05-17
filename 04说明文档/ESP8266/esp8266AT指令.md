# ESP8266 连接 OneNET 云平台 —— AT 指令入门（新手版）

> **适合谁读**：第一次用 ESP8266 + OneNET，想先用串口把「连上云、能上报、能收下发」跑通，再去看 MCU 工程代码。  
> **和工程的关系**：本文是 **手工发 AT** 的练手步骤；MCU 里由 `esp8266_mqtt.c` 自动发同样的 AT。更完整的任务分工见同目录《ESP8266-OneNET-MQTT与FreeRTOS任务说明.md》。

---

## 1. 先搞懂三件事（不用背代码）

| 角色 | 干什么 |
|------|--------|
| **OneNET 云平台** | 存设备数据、显示温湿度、下发「开灯/关灯」等指令 |
| **ESP8266 模块** | 连家里 WiFi，再连 OneNET 的 MQTT 服务器；内部已实现 MQTT，你只需发 **AT 字符串** |
| **STM32（本工程）** | 通过 **USART3** 给 ESP8266 发 AT；自己 **不跑** TCP/MQTT 协议栈 |

数据走向可以记成：

```text
传感器/LED  ←→  STM32  ←→(串口 AT)←→  ESP8266  ←→(WiFi/MQTT)←→  OneNET
```

---

## 2. 开始之前要准备好的东西

### 2.1 硬件

- ESP8266 模块与 STM32：**TX/RX 交叉、共地**，供电 **3.3V**（电流要够）。
- 第一次建议：**USB 转 TTL 只接 ESP8266** 做实验（不要和 MCU 同时抢串口）。
- 串口参数：**115200，8N1**（与工程 `esp8266_init(115200)` 一致）。

### 2.2 OneNET 控制台里要抄下来的参数

登录 [OneNET](https://open.iot.10086.cn/) → 你的产品 → 设备，准备好下表（**换成你自己的**）：

| 在平台叫什么 | 填到 AT / 代码里 | 本工程示例 |
|--------------|------------------|------------|
| 产品 ID | MQTT 用户名、主题里的 `{pid}` | `y7l1o7W636` |
| 设备名称 | ClientID、主题里的 `{device-name}` | `test` |
| 设备密钥 / Token | `AT+MQTTUSERCFG` 的密码（一长串） | 见下文命令 |
| 路由器 SSID / 密码 | `AT+CWJAP` | 自行填写 |

### 2.3 本工程当前烧录的固件版本（`AT+GMR`）

串口发送 `AT+GMR`，本工程模块实测回显如下（**请与你自己模块核对**；若不一致，MQTT 相关 AT 行为可能略有差别）：

```text
AT+GMR
AT version:2.3.0.0-dev(s-bcd64d2 - ESP8266 - Jun 23 2021 11:42:05)
SDK version:v3.4-22-g967752e2
compile time(b498b58):Jun 30 2021 11:28:20
Bin version:2.2.0(ESP8266_1MB)

OK
```

| 字段 | 值 | 说明 |
|------|-----|------|
| AT version | 2.3.0.0-dev | 乐鑫 ESP-AT 开发分支版本号 |
| SDK version | v3.4-22-g967752e2 | 底层 SDK |
| compile time | Jun 30 2021 11:28:20 | 固件编译时间 |
| Bin version | **2.2.0(ESP8266_1MB)** | 实际烧录的 AT 固件包（1MB Flash） |

属于 **乐鑫 ESP-AT 的 MQTT 固件**，支持 `AT+MQTTUSERCFG`、`AT+MQTTCONN`、`AT+MQTTPUB`、`AT+MQTTPUBRAW`、`AT+MQTTSUB` 等；本文后续命令均按该版本整理。

---

## 3. 推荐学习顺序（强烈建议按步做）

```text
第 1 步：AT、连 WiFi、能拿到 IP
第 2 步：MQTTUSERCFG + MQTTCONN，连上 OneNET
第 3 步：MQTTPUB 上报一条温度，控制台能看到数据
第 4 步：MQTTSUB 订阅「下发」和「上报回执」主题
第 5 步：（可选）用平台「属性设置」测控灯，并理解为什么要 set_reply
第 6 步：再看 MCU 工程 esp8266_mqtt.c / main.c 里如何用同样 AT 自动化
```

**不要跳步**：WiFi 没 `GOT IP` 就发 MQTT，后面一定失败。

---

## 4. 第 1 步：基础 AT 与 WiFi

在串口助手里 **每条命令单独发送**，等出现 `OK` 或 `GOT IP` 再发下一条。  
命令末尾需要 **回车换行**（\r\n），多数助手勾选「发送新行」即可。

| 顺序 | 发送的命令 | 作用 | 正常时应看到 |
|------|------------|------|----------------|
| 1 | `AT` | 测试模块是否存活 | `OK` |
| 2 | `AT+GMR` | 查版本 | 版本信息 + `OK` |
| 3 | `AT+CWMODE=1` | STA 模式（连路由器） | `OK` |
| 4 | `AT+CWJAP="你的WiFi名","你的WiFi密码"` | 连路由器 | `WIFI CONNECTED` → `WIFI GOT IP` → `OK` |
| 5 | `AT+CIPSTA?` | 查看 IP（可选） | `+CIPSTA:ip:"..."` 等 |

示例（请改成你的 WiFi）：

```text
AT+CWJAP="WZG","qwertyuiop123"
```

> **说明**：`AT+CWLAP` 可扫描热点；`AT+CIFSR` 在部分固件上也能查 IP，以你模块实际支持的为准。

---

## 5. 第 2 步：连接 OneNET（MQTT）

### 5.1 配置 MQTT 用户（鉴权）

**整行复制发送，不要断行**（密码是一整段 token）：

```text
AT+MQTTUSERCFG=0,1,"test","y7l1o7W636","version=2018-10-31&res=products%2Fy7l1o7W636%2Fdevices%2Ftest&et=9999999999&method=md5&sign=cM9C%2BiXlINLMI6r%2BxW9KUg%3D%3D",0,0,""
```

参数含义（新手只需记对应关系）：

| 位置 | 本示例 | 含义 |
|------|--------|------|
| 第 1 个 `0` | link ID，固定用 0 | |
| `1` | MQTT 版本 / 类型（按 OneNET 文档） | |
| `"test"` | **ClientID** = 设备名 | |
| `"y7l1o7W636"` | **Username** = 产品 ID | |
| 长字符串 | **Password** = 平台生成的 token | |
| 末尾 `0,0,""` | 一般为默认 | |

期望：`OK`。

### 5.2 连接 Broker

```text
AT+MQTTCONN=0,"mqtts.heclouds.com",1883,0
```

期望：`OK`，并可能出现 `+MQTTCONNECTED:0,1,"mqtts.heclouds.com","1883",...`。

---

## 6. 主题（Topic）是什么？从哪来？

OneNET **新版物模型**规定主题格式，和你在代码里看到的宏是一回事。

**拼法（记住公式即可）：**

```text
$sys/<产品ID>/<设备名>/thing/property/<动作>
```

本工程在 `esp8266_mqtt.h` 里用宏写好（改产品/设备只改头文件）：

| 宏名 | 展开后的主题 | 谁发给谁 | 干什么 |
|------|----------------|----------|--------|
| `MQTT_PUBLISH_TOPIC` | `$sys/y7l1o7W636/test/thing/property/post` | 设备 → 云 | **上报**温湿度、灯状态等 |
| `MQTT_SUBSCRIBE_TOPIC` | `$sys/y7l1o7W636/test/thing/property/set` | 云 → 设备 | **平台下发**控灯等 |
| `MQTT_REPLY_TOPIC` | `$sys/y7l1o7W636/test/thing/property/post/reply` | 云 → 设备 | 上报后的**回执**（可选） |
| `MQTT_SET_REPLY_TOPIC` | `$sys/y7l1o7W636/test/thing/property/set_reply` | 设备 → 云 | **属性设置应答**（`mqtt_handle_property_set` 自动发布） |

在 MCU 里订阅时代码类似：

```c
mqtt_subscribe_topic(MQTT_SUBSCRIBE_TOPIC, 0, 1);
// 内部会 sprintf 成：AT+MQTTSUB=0,"$sys/.../thing/property/set",0
```

也就是说：**topic 不是模块“算”出来的，而是你把平台规定的字符串通过 AT 告诉模块去订阅/发布。**

---

## 7. 第 3 步：向云端上报属性（设备 → 云）

### 7.1 用 `AT+MQTTPUB`（适合较短 JSON）

上报主题：

```text
$sys/y7l1o7W636/test/thing/property/post
```

**重要：JSON 里的逗号在 AT 里要写成 `\,`**，否则模块会把逗号当成 AT 参数分隔符，返回 `ERROR`。

✅ 正确示例（注意 `\"123\"\,` 里逗号前的反斜杠）：

```text
AT+MQTTPUB=0,"$sys/y7l1o7W636/test/thing/property/post","{\"id\":\"123\"\,\"params\":{\"temperature\":{\"value\":23.0}}}",1,0
```

❌ 常见错误：

- `"123","params"` 中间 **少写 `\`** → `ERROR`
- `23.0` 后面 **`}}}}` 多了一个 `}`** → `ERROR`
- 整行超过约 **256 字节** → 用下面的 `MQTTPUBRAW`

成功：一般先 `OK`，有的固件还有 `+MQTTPUB:OK`。  
然后去 OneNET 控制台看设备属性里温度是否更新。

### 7.2 用 `AT+MQTTPUBRAW`（适合长 JSON，与工程一致）

**第 1 条**：声明主题、长度、QoS（长度 = 后面 JSON **字节数**，必须数准）：

```text
AT+MQTTPUBRAW=0,"$sys/y7l1o7W636/test/thing/property/post",52,1,0
```

**第 2 步**：出现 `>` 后，**原样发送 52 字节**（不要多打回车）：

```json
{"id":"123","params":{"temperature":{"value":23.0}}}}
```

---

## 8. 第 4 步：订阅主题（云 → 设备）

连上 MQTT 后执行（QoS 用 `0` 即可）：

```text
AT+MQTTSUB=0,"$sys/y7l1o7W636/test/thing/property/set",0
```

```text
AT+MQTTSUB=0,"$sys/y7l1o7W636/test/thing/property/post/reply",0
```

| 订阅的主题 | 用途 |
|------------|------|
| `.../property/set` | 平台「属性设置」、控灯指令从这里下来 |
| `.../property/post/reply` | 你上报 `post` 之后，平台给的确认（调试用） |

订阅成功：每条都应 `OK`。  
之后平台下发时，串口可能看到 `+MQTTSUBRECV:...` 一类 URC（不同固件格式略有差异）。

---

## 9. 第 5 步：平台「属性设置」与超时（新手必知）

在 OneNET **设备调试 → 属性设置**里把 `switch_led_1` 设为 `1` 时，平台会：

1. 向 `.../property/set` **发 MQTT 消息**；
2. **等待** 设备向 `.../property/set_reply` **回复** 类似：

```json
{"id":"与下发相同","code":200,"msg":"success"}
```

若几秒内收不到这条回复，平台报：**属性设置失败：设备响应超时（10411）**。

AT+GMR
AT+CWMODE=1
AT+CWJAP="你的WiFi","你的密码"
AT+CIPSTA?

AT+MQTTUSERCFG=0,1,"test","y7l1o7W636","你的token整段",0,0,""
AT+MQTTCONN=0,"mqtts.heclouds.com",1883,0

AT+MQTTPUB=0,"$sys/y7l1o7W636/test/thing/property/post","{\"id\":\"123\"\,\"params\":{\"temperature\":{\"value\":23.0}}}",1,0

AT+MQTTSUB=0,"$sys/y7l1o7W636/test/thing/property/set",0
AT+MQTTSUB=0,"$sys/y7l1o7W636/test/thing/property/post/reply",0
```

---

## 13. 下一步读什么

- **MCU 三任务、队列、优先级**：同目录《ESP8266-OneNET-MQTT与FreeRTOS任务说明.md》
- **改云端账号**：只改 `esp8266_mqtt.h` 里 `MQTT_PRODUCT_ID`、`MQTT_DEVICE_NAME`、`MQTT_DEVICE_KEY` 及 WiFi 宏（在 `esp8266.c` 或配置处）

---

*文档说明：基于本工程 OneNET 物模型与 ESP8266 AT 2.2.x 整理，示例 token/WiFi 仅作格式参考，请勿泄露到公开仓库。*
