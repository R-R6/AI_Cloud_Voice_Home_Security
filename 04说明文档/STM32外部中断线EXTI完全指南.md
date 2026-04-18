# STM32外部中断线(EXTI)完全指南

## 一、EXTI中断线架构概述

### 1.1 中断线数量与映射规则

STM32微控制器提供 **16根外部中断/事件线**（EXTI0 ~ EXTI15），遵循以下映射规则：

```
同一编号的引脚共用同一条中断线，无论它属于哪个GPIO端口

Px0  →  EXTI0   (PA0, PB0, PC0, PD0, PE0, PF0, PG0... 共用EXTI0)
Px1  →  EXTI1   (PA1, PB1, PC1, PD1, PE1, PF1, PG1... 共用EXTI1)
Px2  →  EXTI2   (PA2, PB2, PC2, PD2, PE2, PF2, PG2... 共用EXTI2)
...
Px15 →  EXTI15  (PA15, PB15, PC15, PD15, PE15, PF15, PG15... 共用EXTI15)
```

**核心原则**：
- ✅ **同一时刻，每条EXTI线只能连接一个GPIO引脚**
- ❌ **不能同时将PA0和PB0都连接到EXTI0**
- ⚠️ **需要在SYSCFG中配置具体使用哪个端口的引脚**

### 1.2 中断向量分布

不同系列的STM32，其中断向量分配略有差异：

#### STM32F1/F4系列（常见）
```
EXTI0_IRQn          →  EXTI Line 0
EXTI1_IRQn          →  EXTI Line 1
EXTI2_IRQn          →  EXTI Line 2
EXTI3_IRQn          →  EXTI Line 3
EXTI4_IRQn          →  EXTI Line 4
EXTI9_5_IRQn        →  EXTI Line 5~9 （共享一个中断向量）
EXTI15_10_IRQn      →  EXTI Line 10~15 （共享一个中断向量）
```

#### STM32F7/H7系列（高性能）
```
EXTI0_IRQn          →  EXTI Line 0
EXTI1_IRQn          →  EXTI Line 1
EXTI2_IRQn          →  EXTI Line 2
EXTI3_IRQn          →  EXTI Line 3
EXTI4_IRQn          →  EXTI Line 4
EXTI9_5_IRQn        →  EXTI Line 5~9
EXTI15_10_IRQn      →  EXTI Line 10~15
```

**重要提示**：
- EXTI0~4：每个中断线有独立的中断向量
- EXTI5~9：共享同一个中断向量 `EXTI9_5_IRQn`
- EXTI10~15：共享同一个中断向量 `EXTI15_10_IRQn`

---

## 二、EXTI初始化完整流程

### 2.1 步骤总览

```
1. 使能GPIO时钟
2. 使能SYSCFG时钟
3. 配置GPIO为输入模式
4. 配置GPIO到EXTI的映射（SYSCFG_EXTILineConfig）
5. 配置EXTI参数（触发方式、使能）
6. 配置NVIC中断优先级
7. 编写中断服务函数（ISR）
```

### 2.2 代码实现详解

#### 步骤1：使能时钟

```c
// 使能GPIO端口时钟（根据实际使用的端口选择）
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);  // GPIOA
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);  // GPIOB
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);  // GPIOC
// ... 其他端口

// 使能SYSCFG时钟（关键！用于GPIO到EXTI的映射）
RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
```

**为什么需要SYSCFG时钟？**
- SYSCFG（System Configuration Controller）负责将物理GPIO引脚路由到EXTI中断线
- 未使能SYSCFG时钟会导致`SYSCFG_EXTILineConfig`调用无效

#### 步骤2：配置GPIO

```c
GPIO_InitTypeDef GPIO_InitStructure;

// 以PA0为例
GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;           // 输入模式
GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;       // 高速
GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;           // 上拉（按键常用）
// 或 GPIO_PuPd_NOPULL;                                 // 无上下拉（外部已有电阻）
// 或 GPIO_PuPd_DOWN;                                   // 下拉

GPIO_Init(GPIOA, &GPIO_InitStructure);
```

**GPIO模式选择**：
| 应用场景 | GPIO模式 | 上下拉配置 | 说明 |
|---------|---------|-----------|------|
| 按键检测 | `GPIO_Mode_IN` | `GPIO_PuPd_UP` | 内部上拉，按下接地 |
| 外部传感器 | `GPIO_Mode_IN` | `GPIO_PuPd_NOPULL` | 外部已有上拉/下拉 |
| 电平触发 | `GPIO_Mode_IN` | 根据电路决定 | 跟随外部电平 |

#### 步骤3：GPIO到EXTI映射

```c
// 将PA0连接到EXTI0
SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);

// 将PE2连接到EXTI2
SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE, EXTI_PinSource2);

// 将PC13连接到EXTI13
SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource13);
```

**函数原型**：
```c
void SYSCFG_EXTILineConfig(uint8_t EXTI_PortSourceGPIOx, uint8_t EXTI_PinSourcex);
```

**参数说明**：
- `EXTI_PortSourceGPIOx`：端口源，取值范围：
  - `EXTI_PortSourceGPIOA` ~ `EXTI_PortSourceGPIOK`（取决于芯片型号）
- `EXTI_PinSourcex`：引脚源，取值范围：
  - `EXTI_PinSource0` ~ `EXTI_PinSource15`

**注意事项**：
- 每次调用会覆盖之前的映射关系
- 如果之前PA0已映射到EXTI0，再调用`SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource0)`会将EXTI0切换到PB0
- **同一EXTI线只能有一个有效映射**

#### 步骤4：配置EXTI参数

```c
EXTI_InitTypeDef EXTI_InitStructure;

EXTI_InitStructure.EXTI_Line = EXTI_Line0;              // 选择中断线
EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;     // 中断模式
// 或 EXTI_Mode_Event;                                  // 事件模式（不触发中断）

EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; // 下降沿触发
// 或 EXTI_Trigger_Rising;                              // 上升沿触发
// 或 EXTI_Trigger_Rising_Falling;                      // 双边沿触发

EXTI_InitStructure.EXTI_LineCmd = ENABLE;               // 使能该中断线
// 或 DISABLE;                                          // 禁用

EXTI_Init(&EXTI_InitStructure);
```

**触发方式选择指南**：

| 触发方式 | 适用场景 | 优点 | 缺点 |
|---------|---------|------|------|
| `EXTI_Trigger_Falling` | 按键按下、低电平有效信号 | 响应及时，符合直觉 | 需注意消抖 |
| `EXTI_Trigger_Rising` | 按键释放、高电平有效信号 | 避免按下抖动 | 响应稍延迟 |
| `EXTI_Trigger_Rising_Falling` | 需要检测按下和释放 | 信息完整 | 可能触发两次，需软件区分 |

**中断模式 vs 事件模式**：
- **中断模式**（`EXTI_Mode_Interrupt`）：触发CPU中断，执行ISR
- **事件模式**（`EXTI_Mode_Event`）：仅产生事件脉冲，可用于唤醒DMA、定时器等，不占用CPU

#### 步骤5：配置NVIC

```c
NVIC_InitTypeDef NVIC_InitStructure;

// 对于EXTI0~4，使用独立中断向量
NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x05;  // 抢占优先级
NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;         // 子优先级
NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
NVIC_Init(&NVIC_InitStructure);

// 对于EXTI5~9，共享中断向量
NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
NVIC_Init(&NVIC_InitStructure);

// 对于EXTI10~15，共享中断向量
NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
NVIC_Init(&NVIC_InitStructure);
```

**FreeRTOS环境下的特殊配置**：

```c
// 如果中断服务函数中需要调用FreeRTOS API
NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 
    configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
```

**优先级数值说明**：
- Cortex-M内核：**数值越小，优先级越高**
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`：允许调用FreeRTOS API的最高优先级（最低数值）
- 高于此优先级的中断**不能**调用FreeRTOS API

---

## 三、中断服务函数（ISR）编写

### 3.1 独立中断向量的ISR（EXTI0~4）

```c
void EXTI0_IRQHandler(void)
{
    // 1. 检查中断标志
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 2. 清除pending位（防止重复触发）
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 3. 执行用户逻辑
        // ... 你的代码 ...
        
        // 示例：切换LED状态
        GPIO_ToggleBits(GPIOA, GPIO_Pin_5);
    }
}
```

### 3.2 共享中断向量的ISR（EXTI5~9）

```c
void EXTI9_5_IRQHandler(void)
{
    // 必须逐个检查是哪条线触发的中断
    
    // 检查EXTI5
    if(EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        // 处理EXTI5的逻辑
        handle_exti5_event();
    }
    
    // 检查EXTI6
    if(EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line6);
        // 处理EXTI6的逻辑
        handle_exti6_event();
    }
    
    // 检查EXTI7
    if(EXTI_GetITStatus(EXTI_Line7) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line7);
        handle_exti7_event();
    }
    
    // 检查EXTI8
    if(EXTI_GetITStatus(EXTI_Line8) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line8);
        handle_exti8_event();
    }
    
    // 检查EXTI9
    if(EXTI_GetITStatus(EXTI_Line9) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line9);
        handle_exti9_event();
    }
}
```

**关键点**：
- 共享中断向量时，**必须检查所有可能的中断线**
- 使用`if`而非`else if`，因为可能同时触发多条线
- 每条线都要单独清除pending位

### 3.3 EXTI10~15的ISR

```c
void EXTI15_10_IRQHandler(void)
{
    // 检查EXTI10~15
    if(EXTI_GetITStatus(EXTI_Line10) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line10);
        handle_exti10_event();
    }
    
    if(EXTI_GetITStatus(EXTI_Line11) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line11);
        handle_exti11_event();
    }
    
    // ... 类似处理EXTI12~15
}
```

### 3.4 FreeRTOS环境下的ISR

```c
void EXTI0_IRQHandler(void)
{
    uint32_t ulReturn;
    
    // 进入临界区（保护FreeRTOS内核数据结构）
    ulReturn = taskENTER_CRITICAL_FROM_ISR();
    
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 调用FreeRTOS API（FromISR版本）
        xSemaphoreGiveFromISR(xBinarySemaphore, NULL);
        // 或
        xEventGroupSetBitsFromISR(xEventGroup, EVENT_BIT, NULL);
        // 或
        xQueueSendFromISR(xQueue, &data, NULL);
    }
    
    // 退出临界区
    taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}
```

**注意事项**：
- 必须使用`taskENTER_CRITICAL_FROM_ISR()`和`taskEXIT_CRITICAL_FROM_ISR()`
- 只能调用带`FromISR`后缀的FreeRTOS API
- 临界区应尽量短，避免影响系统实时性

---

## 四、实际应用案例

### 4.1 案例1：单个按键检测

**需求**：检测PA0按键按下，点亮LED

```c
#include "stm32f4xx.h"

void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    // 2. 配置PA0为上拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 3. 映射PA0到EXTI0
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
    
    // 4. 配置EXTI0
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    // 5. 配置NVIC
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 简单消抖
        Delay_ms(20);
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 0)
        {
            // 点亮LED
            GPIO_SetBits(GPIOD, GPIO_Pin_12);
        }
    }
}
```

### 4.2 案例2：多个按键（不同端口）

**需求**：检测PA0、PB1、PC2三个按键

```c
void MultiKey_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能所有相关时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | 
                           RCC_AHB1Periph_GPIOB | 
                           RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    // 2. 配置三个按键引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    // 3. 映射到对应的EXTI线
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource1);
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource2);
    
    // 4. 配置EXTI（可以一次性配置多条线）
    EXTI_InitStructure.EXTI_Line = EXTI_Line0 | EXTI_Line1 | EXTI_Line2;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    // 5. 分别配置三个NVIC
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_Init(&NVIC_InitStructure);
    
    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_Init(&NVIC_InitStructure);
    
    NVIC_InitStructure.NVIC_IRQChannel = EXTI2_IRQn;
    NVIC_Init(&NVIC_InitStructure);
}

// 三个独立的中断服务函数
void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        HandleKey0();
    }
}

void EXTI1_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);
        HandleKey1();
    }
}

void EXTI2_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line2) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line2);
        HandleKey2();
    }
}
```

### 4.3 案例3：共享中断向量的多路检测

**需求**：检测PA5、PB6、PC7三个传感器信号

```c
void Sensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | 
                           RCC_AHB1Periph_GPIOB | 
                           RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    // 2. 配置GPIO（假设是外部上拉，所以用NOPULL）
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    // 3. 映射
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource5);
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource6);
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource7);
    
    // 4. 配置EXTI5~7（它们共享EXTI9_5_IRQn）
    EXTI_InitStructure.EXTI_Line = EXTI_Line5 | EXTI_Line6 | EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;  // 上升沿
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    // 5. 配置共享的NVIC
    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x03;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

// 共享的中断服务函数
void EXTI9_5_IRQHandler(void)
{
    // 检查EXTI5
    if(EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        ProcessSensor5();  // 处理PA5的传感器
    }
    
    // 检查EXTI6
    if(EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line6);
        ProcessSensor6();  // 处理PB6的传感器
    }
    
    // 检查EXTI7
    if(EXTI_GetITStatus(EXTI_Line7) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line7);
        ProcessSensor7();  // 处理PC7的传感器
    }
}
```

### 4.4 案例4：动态切换EXTI映射

**需求**：运行时动态切换使用PA0还是PB0

```c
void Switch_EXTI_Source(uint8_t use_port_a)
{
    // 1. 先禁用EXTI0
    EXTI_LineCmd(EXTI_Line0, DISABLE);
    
    // 2. 重新映射
    if(use_port_a)
    {
        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
    }
    else
    {
        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource0);
    }
    
    // 3. 重新使能
    EXTI_LineCmd(EXTI_Line0, ENABLE);
}
```

**应用场景**：
- 多路复用传感器选择
- 硬件版本兼容（不同版本使用不同引脚）
- 故障切换（主引脚损坏时切换到备用引脚）

---

## 五、常见问题与解决方案

### 5.1 问题1：中断不触发

**可能原因**：
1. ❌ 忘记使能SYSCFG时钟
2. ❌ GPIO未正确配置为输入模式
3. ❌ EXTI映射错误（端口或引脚号不匹配）
4. ❌ NVIC未使能
5. ❌ 触发方式与实际信号不符

**排查步骤**：
```c
// 1. 在中断服务函数开头添加调试输出
void EXTI0_IRQHandler(void)
{
    printf("EXTI0 ISR entered!\r\n");  // 确认是否进入ISR
    
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        printf("EXTI0 triggered!\r\n");  // 确认是否是EXTI0触发
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

// 2. 在主循环中读取GPIO电平，确认硬件正常
while(1)
{
    printf("PA0 level: %d\r\n", GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0));
    Delay_ms(500);
}
```

### 5.2 问题2：中断频繁触发（抖动）

**现象**：按一次键，中断触发多次

**解决方案**：

**方案A：硬件消抖**
```
在按键与地之间并联电容（10nF~100nF）
```

**方案B：软件消抖（推荐）**
```c
static uint32_t last_interrupt_time = 0;

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        uint32_t current_time = HAL_GetTick();
        
        // 距离上次中断不足50ms，忽略
        if((current_time - last_interrupt_time) < 50)
        {
            return;
        }
        
        last_interrupt_time = current_time;
        
        // 执行实际逻辑
        HandleKeyPress();
    }
}
```

**方案C：延时+二次确认**
```c
void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 禁用中断
        NVIC_DisableIRQ(EXTI0_IRQn);
        
        // 延时消抖
        Delay_ms(20);
        
        // 再次确认
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 0)
        {
            HandleKeyPress();
        }
        
        // 重新使能中断
        NVIC_EnableIRQ(EXTI0_IRQn);
    }
}
```

### 5.3 问题3：共享中断向量中某条线不响应

**可能原因**：
- 未在该线的ISR分支中清除pending位
- 其他线的处理时间过长，导致新中断丢失

**解决方案**：
```c
void EXTI9_5_IRQHandler(void)
{
    // 快速清除所有pending位，再处理逻辑
    uint32_t pending_lines = EXTI->PR & 0x03E0;  // 获取EXTI5~9的pending状态
    
    if(pending_lines & EXTI_Line5)
    {
        EXTI->PR = EXTI_Line5;  // 直接写寄存器清除（更快）
        // 不要在这里做耗时操作，改为设置标志
        sensor5_flag = 1;
    }
    
    if(pending_lines & EXTI_Line6)
    {
        EXTI->PR = EXTI_Line6;
        sensor6_flag = 1;
    }
    
    // ... 其他线
}

// 在主循环或低优先级任务中处理
void MainLoop(void)
{
    if(sensor5_flag)
    {
        sensor5_flag = 0;
        ProcessSensor5();  // 耗时操作放在这里
    }
    
    if(sensor6_flag)
    {
        sensor6_flag = 0;
        ProcessSensor6();
    }
}
```

### 5.4 问题4：FreeRTOS中调用API导致HardFault

**原因**：
- 中断优先级高于`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`
- 使用了非FromISR版本的API

**解决方案**：
```c
// 错误示例
void EXTI0_IRQHandler(void)
{
    vTaskDelay(100);  // ❌ 不能在中断中调用阻塞API
    xSemaphoreGive(xSem);  // ❌ 不能用普通版本
}

// 正确示例
void EXTI0_IRQHandler(void)
{
    uint32_t ulReturn = taskENTER_CRITICAL_FROM_ISR();
    
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xSem, &xHigherPriorityTaskWoken);  // ✅ 使用FromISR版本
        
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);  // 如果需要切换任务
    }
    
    taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}
```

---

## 六、高级应用技巧

### 6.1 使用事件模式唤醒低功耗

```c
void EnterLowPowerMode(void)
{
    EXTI_InitTypeDef EXTI_InitStructure;
    
    // 配置为事件模式（不触发中断）
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;  // 事件模式
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    // 进入STOP模式，等待事件唤醒
    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
    
    // 被唤醒后恢复系统时钟
    SystemInit();
}
```

### 6.2 边缘检测计数器

```c
volatile uint32_t rising_count = 0;
volatile uint32_t falling_count = 0;

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 读取当前电平判断是上升沿还是下降沿
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0))
        {
            rising_count++;   // 上升沿计数
        }
        else
        {
            falling_count++;  // 下降沿计数
        }
    }
}
```

### 6.3 脉冲宽度测量

```c
static uint32_t rise_time = 0;
static uint32_t pulse_width = 0;

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        uint32_t current_time = DWT->CYCCNT;  // 使用DWT计数器
        
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0))
        {
            // 上升沿，记录时间
            rise_time = current_time;
        }
        else
        {
            // 下降沿，计算脉宽
            pulse_width = current_time - rise_time;
            
            // 转换为微秒（假设168MHz主频）
            float width_us = (float)pulse_width / 168.0f;
            printf("Pulse width: %.2f us\r\n", width_us);
        }
    }
}
```

### 6.4 看门狗喂狗触发

```c
void EXTI15_10_IRQHandler(void)
{
    // 使用PC13作为外部喂狗信号
    if(EXTI_GetITStatus(EXTI_Line13) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line13);
        
        // 外部信号触发喂狗
        IWDG_ReloadCounter();
        
        // 可选：记录喂狗次数
        watchdog_feed_count++;
    }
}
```

---

## 七、性能优化建议

### 7.1 减少ISR执行时间

**原则**：ISR应该尽可能短小快速

```c
// ❌ 错误示例：在ISR中做耗时操作
void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 不要在ISR中做这些！
        printf("Key pressed!\r\n");      // ❌ 串口发送很慢
        OLED_ShowString(0, 0, "Press");  // ❌ OLED刷新慢
        Delay_ms(100);                   // ❌ 绝对禁止延时
    }
}

// ✅ 正确示例：只设置标志，在主循环处理
volatile uint8_t key_pressed_flag = 0;

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        key_pressed_flag = 1;  // 快速设置标志
    }
}

// 主循环中处理
void MainLoop(void)
{
    if(key_pressed_flag)
    {
        key_pressed_flag = 0;
        printf("Key pressed!\r\n");      // ✅ 在主循环中执行
        OLED_ShowString(0, 0, "Press");  // ✅ 安全
    }
}
```

### 7.2 使用直接寄存器访问提升速度

```c
// 标准库方式（较慢）
if(EXTI_GetITStatus(EXTI_Line0) != RESET)
{
    EXTI_ClearITPendingBit(EXTI_Line0);
}

// 直接寄存器方式（更快）
if(EXTI->PR & EXTI_Line0)  // 直接读pending寄存器
{
    EXTI->PR = EXTI_Line0;  // 直接写pending寄存器清除
}
```

### 7.3 批量清除pending位

```c
// 如果需要同时清除多条线
EXTI->PR = EXTI_Line5 | EXTI_Line6 | EXTI_Line7;  // 一次性清除
```

---

## 八、不同STM32系列的差异

### 8.1 STM32F1系列
- EXTI0~15：16条线
- 中断向量：EXTI0~4独立，EXTI9_5共享，EXTI15_10共享
- SYSCFG称为AFIO（Alternate Function I/O）

```c
// F1系列使用AFIO
RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
```

### 8.2 STM32F4系列
- EXTI0~15：16条线
- 额外支持PVD、RTC闹钟、以太网等内部事件
- 使用SYSCFG

```c
// F4系列使用SYSCFG
RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
```

### 8.3 STM32H7系列
- EXTI0~15：16条线
- 支持更多内部事件源
- 中断向量更细化（部分型号EXTI5~9也分开）

---

## 九、最佳实践总结

### 9.1 设计 checklist

- [ ] 使能了对应GPIO端口的时钟
- [ ] 使能了SYSCFG（或AFIO）时钟
- [ ] GPIO配置为输入模式
- [ ] 正确设置了上下拉电阻
- [ ] 使用`SYSCFG_EXTILineConfig`完成映射
- [ ] EXTI触发方式与实际信号匹配
- [ ] NVIC优先级配置合理
- [ ] ISR中清除了pending位
- [ ] 实现了消抖机制（如需）
- [ ] FreeRTOS环境下使用了正确的API和临界区

### 9.2 代码规范

```c
// 1. 命名规范
void KEY_EXTI_Init(void);              // 初始化函数
void EXTI0_IRQHandler(void);           // 中断服务函数
static void HandleKey0Event(void);     // 事件处理函数

// 2. 注释规范
/**
 * @brief  EXTI0中断服务函数
 * @note   检测PA0按键按下，设置事件标志
 */
void EXTI0_IRQHandler(void)
{
    // ...
}

// 3. 错误处理
if(EXTI_GetITStatus(EXTI_Line0) != RESET)
{
    EXTI_ClearITPendingBit(EXTI_Line0);
    // 添加返回值检查或日志
}
```

### 9.3 调试技巧

```c
// 1. 使用逻辑分析仪观察引脚波形
// 2. 在ISR中翻转GPIO，用示波器测量ISR执行时间
void EXTI0_IRQHandler(void)
{
    GPIO_SetBits(GPIOD, GPIO_Pin_15);   // ISR开始，置高
    
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        // 处理逻辑
    }
    
    GPIO_ResetBits(GPIOD, GPIO_Pin_15); // ISR结束，置低
}
// 用示波器测量PD15的高电平持续时间

// 3. 统计中断触发次数
volatile uint32_t exti0_count = 0;
void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        exti0_count++;  // 统计次数
    }
}
```

---

## 十、总结

STM32的外部中断系统提供了灵活高效的异步事件处理能力：

### 核心要点
1. **16条EXTI线**，按引脚编号分组（Px0→EXTI0）
2. **同一时刻每条EXTI线只能映射一个GPIO引脚**
3. **EXTI0~4有独立中断向量，EXTI5~9和EXTI10~15共享**
4. **必须使能SYSCFG时钟并完成GPIO到EXTI的映射**
5. **ISR中必须清除pending位，否则中断会持续触发**

### 应用场景
- ✅ 按键检测（最常用）
- ✅ 外部传感器信号捕获
- ✅ 编码器脉冲计数
- ✅ 低功耗唤醒
- ✅ 紧急停止信号响应

### 与其他外设对比

| 特性 | EXTI | 定时器输入捕获 | DMA传输 |
|-----|------|--------------|--------|
| 响应速度 | 最快（立即触发） | 较快 | 较慢 |
| CPU占用 | 低（仅ISR期间） | 低 | 最低 |
| 精度 | 中等（受ISR延迟影响） | 高 | 不适用 |
| 适用频率 | <10kHz | <1MHz | 任意 |
| 复杂度 | 简单 | 中等 | 复杂 |

掌握EXTI的使用，是实现高效嵌入式系统设计的基础技能！🎯
