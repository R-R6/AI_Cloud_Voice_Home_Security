/****************************************************************
*名    称:基于STM32-FreeRTOS系统的智能居家安防系统
*作    者:吴兆国 神中神
*创建日期:2025/11/2

*功能介绍:
	1.启动安防系统
		RFID启动
			默认卡，则嘀一声示意，LED0闪烁一次。
			非法卡，则嘀两声，LED1闪烁一次。
	2.启动后进入主界面
		可操作按键进行菜单浏览，查看时间、温湿度、安防状态等。主菜单按两次返回键锁屏
	3.超声波
		超声波模块探测距离小于预设的报警距离，LED2常亮，蜂鸣器开启。
	4.火焰报警
		火焰传感器检测到超过危险范围时，火警灯常亮，蜂鸣器开启。
	5.烟雾报警
		烟雾传感器传感器检测到超过危险范围时，蜂鸣器开启。
	6.手机蓝牙
		可查询当前安防状态（火焰预警、可燃气体是否超标、温湿度状态）
		修改开发板的RTC日期时间
		修改超声波模块报警距离(存储eeprom，到修改成功则蜂鸣器嘀两声示意)
		修改安防系统默认卡ID(存储eeprom，到修改成功则蜂鸣器嘀两声示意)
	7.数据存储
		将超声波模块报警距离保存到eeprom
		将安防系统默认卡ID保存到eeprom，如果用户没有通过蓝牙修改有效卡，app_task_init 初始化写入默认卡（白卡），以用户配置的优先。
	8.语音识别
		（1）控制客厅灯的亮灭(LED3)
		（2）播报当前时间
		（3）播报当前温湿度值
		（4）播报当前安防状态（火焰预警、可燃气体是否超标）
	9.临界区
		（1）关键变量、代码的保护
		（2）看门狗喂狗		
	10.软件定时器
		（1）软件定时器的创建与配置
		（2）软件定时器的回调函数实现独立看门狗喂狗操作	
	11.空闲任务钩子函数
		（1）一旦进入空闲任务，则让CPU进入睡眠模式，降低功耗
*说  明:		
	当前代码尽可能实现了模块化编程，一个任务管理一个硬件。通过消息队列异步通信保护OLED资源。
*****************************************************************/

#include "includes.h"

/* 全局变量 */
const uint8_t defalut_distance[1]={0x32}; 			// 默认安全距离 50mm
volatile uint8_t g_alarm_distance = 50;            // 全局报警距离，默认50mm（和默认值一致）
volatile uint8_t g_valid_card_id[5] = {0};  	   // 全局有效卡ID数组（5字节，对应RFID卡号长度）
volatile uint8_t g_fire_status = 0;  // 0=安全，1=报警
volatile uint8_t g_mq2_status = 0;   // 0=安全，1=报警


//MFRC522数据区
volatile u8  mfrc552pidbuf[18];
volatile u8  card_pydebuf[2];
volatile u8  card_numberbuf[5];  								//卡序列号
volatile u8  card_key0Abuf[6]={0xff,0xff,0xff,0xff,0xff,0xff};  //验证卡号的字符串
volatile u8  card_writebuf[16]={0};  							//写入卡片的数据
volatile u8  card_readbuf[18];  								//读出卡片的数据
const uint8_t defalut_cardnum[5]={0x9C,0x4B,0x8E,0x05,0x5C};	//默认有效卡号
// 白卡：有效卡 0x9C,0x4B,0x8E,0x05,0x5C  
// 蓝卡：无效卡 0xD6 0x38 0xF3 0x05 0x18 

//任务句柄
static TaskHandle_t app_task_init_handle = NULL;
static TaskHandle_t app_task_oled_handle = NULL;
static TaskHandle_t app_task_rfid_handle = NULL;
static TaskHandle_t app_task_eeprom_handle = NULL;
static TaskHandle_t app_task_led_handle = NULL;
static TaskHandle_t app_task_beep_handle = NULL;
static TaskHandle_t app_task_rtc_handle = NULL;
static TaskHandle_t app_task_key_handle = NULL;
static TaskHandle_t app_task_sr04_handle = NULL;
static TaskHandle_t app_task_lm393_handle = NULL;
static TaskHandle_t app_task_mq2_handle = NULL;
static TaskHandle_t app_task_dht11_handle = NULL;
static TaskHandle_t app_task_bluetooth_handle = NULL;
static TaskHandle_t app_task_aspro_handle = NULL;


/* 任务 硬件/任务初始化 */ 
static void app_task_init(void* pvParameters); 

/* 任务 OLED */  
static void app_task_oled(void* pvParameters);

/* 任务 RFID */
static void app_task_rfid(void* pvParameters); 

/* 任务 EEPROM */
static void app_task_eeprom(void* pvParameters); 

/* 任务 LED */  
static void app_task_led(void* pvParameters); 

/* 任务 BEEP */  
static void app_task_beep(void* pvParameters);

/* 任务 RTC时钟 */  
static void app_task_rtc(void* pvParameters);

/* 任务 KEY按键 */  
static void app_task_key(void* pvParameters);

/* 任务 超声波 */ 
static void app_task_sr04(void* pvParameters);  

/* 任务 火焰传感器 */ 
static void app_task_lm393(void* pvParameters);  

/* 任务 气体传感器 */ 
static void app_task_mq2(void* pvParameters);  

/* 任务 温湿度传感器 */ 
static void app_task_dht11(void* pvParameters);  

/* 任务 蓝牙 */ 
static void app_task_bluetooth(void* pvParameters);  

/* 任务 语音识别 */ 
static void app_task_aspro(void* pvParameters);  


/* 互斥型信号量句柄 */
SemaphoreHandle_t g_mutex_printf=NULL; 
SemaphoreHandle_t g_mutex_card=NULL;   // 保护全局卡ID数组
SemaphoreHandle_t g_mutex_alarm=NULL;  // 保护全局火警和烟雾报警状态

/* 事件标志组句柄 */
EventGroupHandle_t g_event_group=NULL;

/* 消息队列句柄 */
QueueHandle_t g_queue_oled=NULL;
QueueHandle_t g_queue_eeprom=NULL;
QueueHandle_t g_queue_beep=NULL;
QueueHandle_t g_queue_led=NULL;
QueueHandle_t g_queue_fire=NULL;
QueueHandle_t g_queue_mq2=NULL;
QueueHandle_t g_queue_dht11=NULL;

/* 二值信号量句柄 */
// 用于通知状态变化（蓝牙和语音各一套，避免抢信号）
SemaphoreHandle_t g_sem_fire_bt;  // 火焰状态信号量（蓝牙用）
SemaphoreHandle_t g_sem_fire_asr; // 火焰状态信号量（语音用）
SemaphoreHandle_t g_sem_mq2_bt;   // 烟雾状态信号量（蓝牙用）
SemaphoreHandle_t g_sem_mq2_asr;  // 烟雾状态信号量（语音用）

#define DEBUG_PRINTF_EN	1
void dgb_printf_safe(const char *format, ...)
{
#if DEBUG_PRINTF_EN	

	va_list args;
	va_start(args, format);
	
	/* 获取互斥信号量 */
	xSemaphoreTake(g_mutex_printf,portMAX_DELAY);
	
	vprintf(format, args);
			
	/* 释放互斥信号量 */
	xSemaphoreGive(g_mutex_printf);	

	va_end(args);
#else
	(void)0;
#endif
}

/* 软件定时器句柄 */
static TimerHandle_t soft_timer_Handle =NULL;  

/* 使用软件定时器给iwdg喂狗 */
static void soft_timer_callback(void* parameter)
{		
	/* 关闭中断 */
	portDISABLE_INTERRUPTS();
	
	/* 喂狗，刷新自身计数值 */
	IWDG_ReloadCounter();	
	
	/* 打开中断 */
	portENABLE_INTERRUPTS();
} 
	
int main(void)
	{
	/* 设置FreeRTOS系统中断优先级分组只能选择第4组 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	
	/* 系统定时器中断频率为configTICK_RATE_HZ 168000000/1000 */
	SysTick_Config(SystemCoreClock/configTICK_RATE_HZ);									
	
	/* 初始化串口1 */
	uart1_init(9600);  

	/* 创建 app_task_init任务 */
	xTaskCreate((TaskFunction_t )app_task_init,  		/* 任务入口函数 */
			  (const char*    )	"app_task_init",		/* 任务名字 */
			  (uint16_t       )	512,  					/* 任务栈大小 */
			  (void*          )	NULL,					/* 任务入口函数参数 */
			  (UBaseType_t    )	5, 						/* 任务的优先级 */
			  (TaskHandle_t*  )&app_task_init_handle);	/* 任务控制块指针 */ 	
			
	/* 开启任务调度 */
	vTaskStartScheduler(); 		
			  
	while(1);
}


/* 任务列表 */
static const task_t task_tbl[] = {
	{app_task_oled,        "app_task_oled",        512,    NULL,    5,  &app_task_oled_handle},
	{app_task_rfid,        "app_task_rfid",        512,    NULL,    5,  &app_task_rfid_handle},
	{app_task_eeprom,      "app_task_eeprom",      512,    NULL,    5,  &app_task_eeprom_handle},
	{app_task_led,         "app_task_led",         512,    NULL,    5,  &app_task_led_handle},
	{app_task_beep,        "app_task_beep",        512,    NULL,    5,  &app_task_beep_handle},
	{app_task_rtc,         "app_task_rtc",         512,    NULL,    5,  &app_task_rtc_handle},
	{app_task_key,         "app_task_key",         512,    NULL,    5,  &app_task_key_handle},
	{app_task_sr04,        "app_task_sr04",        512,    NULL,    5,  &app_task_sr04_handle},
	{app_task_lm393,       "app_task_lm393",       512,    NULL,    5,  &app_task_lm393_handle},
	{app_task_mq2,         "app_task_mq2",         512,    NULL,    5,  &app_task_mq2_handle},
	{app_task_dht11,       "app_task_dht11",       512,    NULL,    5,  &app_task_dht11_handle},
	{app_task_bluetooth,   "app_task_bluetooth",   512,    NULL,    5,  &app_task_bluetooth_handle},
	{app_task_aspro,       "app_task_aspro",       512,    NULL,    5,  &app_task_aspro_handle},
	
	{0, 0, 0, 0, 0, 0} // 列表结束标志
};


/*	主界面和创建任务	*/
static void app_task_init(void* pvParameters)
{
	uint32_t i;
	uint8_t eeprom_temp[5] = {0};  // 临时缓冲区，读取EEPROM中RFID卡的初始状态
	uint8_t read_distance = 0;  // 存储从EEPROM读取的距离值
	uint8_t temp_card[5]; // 普通临时数组
	
	printf("[app_task_init] create success\r\n");	
	
	/* 创建互斥型信号量(互斥锁) */	  
	g_mutex_printf=xSemaphoreCreateMutex();	
	g_mutex_card = xSemaphoreCreateMutex();  // 专门保护有效卡ID的互斥锁
	g_mutex_alarm = xSemaphoreCreateMutex();
	
	// 创建二值信号量（初始状态为“不可用”）
    g_sem_fire_bt = xSemaphoreCreateBinary();
    g_sem_fire_asr = xSemaphoreCreateBinary();
    g_sem_mq2_bt = xSemaphoreCreateBinary();
    g_sem_mq2_asr = xSemaphoreCreateBinary();
	  
	/* 创建事件标志组 */
	g_event_group=xEventGroupCreate();	
	  
	//创建消息队列，支持多少条消息，每条消息多少个字节
	g_queue_oled=xQueueCreate(16,sizeof(oled_t));	
	g_queue_eeprom=xQueueCreate(16,sizeof(eeprom_t));
	g_queue_led=xQueueCreate(5,1);	
	g_queue_beep=xQueueCreate(5,1);	
	g_queue_fire=xQueueCreate(5,32);
	g_queue_mq2=xQueueCreate(5,32);	
	g_queue_dht11=xQueueCreate(5,5);	
	
		  
	/* 创建周期软件定时器 */
	soft_timer_Handle=xTimerCreate(	(const char*		)"AutoReloadTimer",
									(TickType_t			)1000,	/* 定时器周期 1000(tick) */
									(UBaseType_t		)pdTRUE,/* 周期模式 */
									(void*				)1,		/* 为每个计时器分配一个索引的唯一ID */
									(TimerCallbackFunction_t)soft_timer_callback); 	 
	/* 开启周期软件定时器 */							
	xTimerStart(soft_timer_Handle,0);	
																
	/* 初始化OLED */
	OLED_Init();
	OLED_Clear();
	vTaskDelay(100);	
									
	/* 显示锁屏图标 */
	OLED_DrawBMP(0,0,128,8,(unsigned char *)LOCK_BMP);
	
	/* 显示“欢迎使用智能居家安防系统” */
	OLED_ShowCHinese(48,1,0);
	OLED_ShowCHinese(64,1,1); 
	OLED_ShowCHinese(80,1,2);	
	OLED_ShowCHinese(96,1,3);	
	OLED_ShowCHinese(48,3,4);		
	OLED_ShowCHinese(64,3,5);		
	OLED_ShowCHinese(80,3,6);	
	OLED_ShowCHinese(96,3,7);		
	OLED_ShowCHinese(48,5,8);		
	OLED_ShowCHinese(64,5,9);		
	OLED_ShowCHinese(80,5,10);	
	OLED_ShowCHinese(96,5,11);		
									
	/* 独立看门狗初始化 */		
	iwdg_init();
	
	//led初始化
	led_init();  
	
	//蜂鸣器初始化
	beep_init();  
	
	//初始化实时时钟
	rtc_init();
	
	//按键初始化
	key_init();  
	
	//超声波初始化							
	sr04_init();
	
	//火焰传感器初始化
	lm393_init();
	//启动ADC1的转换
	// ADC_SoftwareStartConv(ADC1);
	
	//气体传感器初始化
	mq2_init();
	//启动ADC2的转换
	//ADC_SoftwareStartConv(ADC1);
	
	//温湿度传感器初始化
	dht11_init();
	
	/* 蓝牙串口初始化 */
	blue_init(9600);
	tim3_init();
	
	/* 语音识别串口初始化 */
	asr_init(9600);
	
	//eeprom初始化
	at24c02_init();
	
	/* 创建用到的任务 */
	i = 0;
	while (task_tbl[i].pxTaskCode)
	{
		xTaskCreate(task_tbl[i].pxTaskCode,		/* 任务入口函数 */
					task_tbl[i].pcName,			/* 任务名字 */
					task_tbl[i].usStackDepth,	/* 任务栈大小 */
					task_tbl[i].pvParameters,	/* 任务入口函数参数 */
					task_tbl[i].uxPriority,		/* 任务的优先级 */
					task_tbl[i].pxCreatedTask); /* 任务控制块指针 */
		i++;
	}
	
	/* 挂起任务 */
	vTaskSuspend(app_task_rtc_handle);	
	vTaskSuspend(app_task_key_handle);	
	vTaskSuspend(app_task_sr04_handle);
	vTaskSuspend(app_task_aspro_handle);	
	vTaskSuspend(app_task_bluetooth_handle);		
//	vTaskSuspend(app_task_lm393_handle);
//	vTaskSuspend(app_task_mq2_handle);	
	
	
	/* 写入初始默认有效卡号 */ 
	// 白卡：有效卡 {0x9C,0x4B,0x8E,0x05,0x5C} => 156-75-142-5-92
	// 蓝卡：无效卡 {0xD6 0x38 0xF3 0x05 0x18} => 214-56-243-5-24

	 // 1. 先读取EEPROM地址0的5字节数据，判断是否为全新未配置状态
    at24c02_read(0, eeprom_temp, 5);  // 读EEPROM初始值
    
    // 2. 判断：若5字节全为0xFF（EEPROM出厂默认值），说明是首次使用，写入默认卡
    if (eeprom_temp[0] == 0xFF && eeprom_temp[1] == 0xFF && 
        eeprom_temp[2] == 0xFF && eeprom_temp[3] == 0xFF && eeprom_temp[4] == 0xFF)
    {
        // 首次初始化：写入默认有效卡ID（如白卡 {0x9C,0x4B,0x8E,0x05,0x5C}）
        at24c02_write(0, (uint8_t *)defalut_cardnum, 5);
        dgb_printf_safe("首次初始化：写入默认有效卡ID\r\n");
		
		memcpy(temp_card, defalut_cardnum, 5);  // 先拷贝到临时数组
		
		// 逐个字符赋值给volatile全局数组
		for (i = 0; i <= 5; i++)
		{
			g_valid_card_id[i] = temp_card[i];
		}
    }
    else
    {
        // 非首次启动，从EEPROM读ID到全局数组（强制类型转换匹配at24c02_read参数）
        dgb_printf_safe("EEPROM已有配置，不覆盖默认卡\r\n");
		at24c02_read(0, (uint8_t *)g_valid_card_id, 5);
    }
	
	// 打印全局数组中的有效卡ID，确认初始化正确
    dgb_printf_safe("全局有效卡ID：");
    for(i=0; i<5; i++)
    {
        dgb_printf_safe("%02X ", g_valid_card_id[i]);
    }
    dgb_printf_safe("\r\n");
	
	
	/* 存储超声波报警距离 */
	// 1. 先读取EEPROM地址5的1字节数据（报警距离存储地址）
	at24c02_read(5, &read_distance, 1);
	
	// 2. 判断：若读到0xFF（EEPROM全新未配置），则写入默认距离；否则跳过
	if (read_distance == 0xFF)
	{
		// 首次使用：写入默认报警距离（defalut_distance=0x32=50mm）到地址5
		at24c02_write(5, (uint8_t *)defalut_distance, 1);
		dgb_printf_safe("首次初始化：写入默认报警距离50mm\r\n");
	}
	else
	{
		// 非首次使用：已有用户配置，不覆盖，打印提示
		dgb_printf_safe("EEPROM已有报警距离配置：%dmm，不覆盖\r\n", read_distance);
	}
	
	// 读取已存距离到全局变量，供超声波任务使用
	at24c02_read(5, (uint8_t *)&g_alarm_distance, 1);
	dgb_printf_safe("当前生效报警距离：%dmm\r\n", g_alarm_distance);
	
	/* 初始化完毕以后删除该任务 */
	vTaskDelete(NULL);	
}

/* 任务 OLED */  
static void app_task_oled(void* pvParameters)
{
	oled_t oled;
	BaseType_t xret=pdFALSE;
	dgb_printf_safe("[app_task_oled] create success\r\n");	
	
	for(;;)
	{
		xret=xQueueReceive(g_queue_oled,&oled,portMAX_DELAY);
		if(xret != pdTRUE)
			continue;
		
		switch(oled.ctrl)
		{
			case OLED_CTRL_DISPLAY_ON:
				/* 亮屏 */
				OLED_Display_On();	
				break;
			
			case OLED_CTRL_DISPLAY_OFF:
				/* 灭屏 */
				OLED_Display_Off();	
				break;
			
			case OLED_CTRL_CLEAR:
				/* 清屏 */
				OLED_Clear();	
				break;
			
			case OLED_CTRL_SHOW_STRING:
				/* 显示字符串 */
				OLED_ShowString(oled.x,oled.y,oled.str,oled.font_size);
				break;
			
			case OLED_CTRL_SHOW_CHINESE:
				/* 显示汉字 */
				OLED_ShowCHinese(oled.x,oled.y,oled.chinese);
				break;				
								
			case OLED_CTRL_SHOW_PICTURE:
				/* 显示图片 */
				OLED_DrawBMP(oled.x,oled.y,oled.x+oled.pic_width,oled.y+oled.pic_height,(unsigned char *)oled.pic);
				break;	
			
			default:
				dgb_printf_safe("[app_task_oled] oled ctrl code is invalid\r\n");
				break;
		}	
	}
}


/* 任务 RFID */
static void app_task_rfid(void* pvParameters)
{
	u32 flag=1;		//无效卡和有效卡反应的标志位
	u32 n=0;		
	u8 i,status,card_size;
	uint8_t led_sta=0x00;
	uint8_t beep_sta=0x00;
	oled_t oled;
	dgb_printf_safe("[app_task_rfid] create success\r\n");	
	for(;;)
	{
		MFRC522_Initializtion();	//初始化读卡器
		status=MFRC522_Request(0x52,(u8 *)card_pydebuf);		//寻卡				
		if(status == 0)	//如果读到卡
		{
			if(n == 0)
			{
				n=1;
				MFRC522_Anticoll((u8 *)card_numberbuf);								//防撞处理			
				card_size=MFRC522_SelectTag((u8 *)card_numberbuf);					//选卡
				MFRC522_Auth(0x60, 4, (u8 *)card_key0Abuf, (u8 *)card_numberbuf);	//验卡
						
				//读卡状态显示，正常为0
				dgb_printf_safe("status=%d\r\n",status);
				
				 // 打印当前读到的卡序列号
                dgb_printf_safe("读到的卡号： ");
                for(i=0; i<5; i++)
                    dgb_printf_safe("%02X ",card_numberbuf[i]);
                dgb_printf_safe("\r\n");
                
                // 打印全局变量中的有效卡ID
                dgb_printf_safe("有效卡ID：   ");
                for(i=0; i<5; i++)
                    dgb_printf_safe("%02X ",g_valid_card_id[i]);
                dgb_printf_safe("\r\n");
				
				xSemaphoreTake(g_mutex_card, portMAX_DELAY);  // 获取互斥锁
				//判断是否为有效卡：直接对比全局数组
				flag=1;
				for(i=0; i<5; i++)
				{
					if(card_numberbuf[i] != g_valid_card_id[i])
						flag=0;
				}
				xSemaphoreGive(g_mutex_card);
				
				if(flag)	//有效卡：刷卡成功
				{
					led_sta=0x0E;	//LED0开
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_led,&led_sta,1000);
					delay_ms(100);
					led_sta=0x0F;	//全灭
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_led,&led_sta,1000);
					
					beep_sta=0x01;	//蜂鸣器开
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);
					delay_ms(100);
					
					beep_sta=0x00;	//蜂鸣器关
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);	
					
					//显示正确图标
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=CORRECT_BMP;
					xQueueSend(g_queue_oled,&oled,1000);
					delay_ms(500);
					//显示主界面
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					/* 恢复rtc、key、bluetooth、aspro任务 */
					vTaskResume(app_task_key_handle);
					vTaskResume(app_task_rtc_handle);
					vTaskResume(app_task_bluetooth_handle);
					vTaskResume(app_task_aspro_handle);	
					
					/* 挂起rfid任务 */
					vTaskSuspend(app_task_rfid_handle);	
				}
				else	//无效卡：刷卡失败
				{
					flag=1;
					dgb_printf_safe("this card is invalid!!!\r\n");
					led_sta=0x0D;	//LED1开
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_led,&led_sta,1000);
					delay_ms(100);
					led_sta=0x0F;	//全灭
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_led,&led_sta,1000);
					
					/* 响两次 */
					beep_sta=0x01;	//蜂鸣器开
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);
					delay_ms(100);
					
					beep_sta=0x00;	//蜂鸣器关
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);	
					delay_ms(100);
					
					beep_sta=0x01;	//蜂鸣器开
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);
					delay_ms(100);
					
					beep_sta=0x00;	//蜂鸣器关
					//发送消息，超时时间为1000个节拍
					xQueueSend(g_queue_beep,&beep_sta,1000);	
					
					//显示错误图标
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=ERROR_BMP;
					xQueueSend(g_queue_oled,&oled,1000);
					delay_ms(500);
					//返回开启界面
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=LOCK_BMP;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_CHINESE;
					oled.x=48;
					oled.y=1;
					oled.chinese=0;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=1;
					oled.chinese=1;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=1;
					oled.chinese=2;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=1;
					oled.chinese=3;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=48;
					oled.y=3;
					oled.chinese=4;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=3;
					oled.chinese=5;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=3;
					oled.chinese=6;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=3;
					oled.chinese=7;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=48;
					oled.y=5;
					oled.chinese=8;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=5;
					oled.chinese=9;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=5;
					oled.chinese=10;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=5;
					oled.chinese=11;
					xQueueSend(g_queue_oled,&oled,1000);
				}
			}
		}
		else
			n=0;
		
		vTaskDelay(50);
	}
}


/* 任务 EEPROM */
static void app_task_eeprom(void* pvParameters)
{
	eeprom_t eeprom;
	uint8_t readbuf[6]={0};
	BaseType_t xret=pdFALSE;
	dgb_printf_safe("[app_task_eeprom] create success\r\n");	
	for(;;)
	{
		// 阻塞等待队列消息（xQueueReceive），接收来自 RFID /超声波 任务的操作请求。
		xret=xQueueReceive(g_queue_eeprom,&eeprom,portMAX_DELAY);
		if(xret != pdTRUE)
			continue;
		
		if(eeprom.ctrl & AT24C02_READ)		//读数据
		{
			at24c02_read(0,readbuf,6);
			eeprom.data=readbuf;
			
			eeprom.ctrl=0x00;
			//发送消息，超时时间为1000个节拍
			xQueueSend(g_queue_eeprom,&eeprom,1000);
			vTaskDelay(100);  //延时一会让rfid接收该数据
		}	
		else	//写数据
			at24c02_write(eeprom.addr,eeprom.data,eeprom.size);
	}
}


/* 任务 LED */
static void app_task_led(void* pvParameters)
{
	char led_sta=0x00;
	BaseType_t rt;
	for(;;)
	{
		rt=xQueueReceive(g_queue_led,&led_sta,portMAX_DELAY);
		if(rt == pdTRUE)
		{
			/* 检测到控制LED0 */
			LED0=led_sta & 0x01 ? 1 : 0;
			/* 检测到控制LED1 */
			LED1=led_sta & 0x02 ? 1 : 0;
			/* 检测到控制LED2 */
			LED2=led_sta & 0x04 ? 1 : 0;
			/* 检测到控制LED3 */
			LED3=led_sta & 0x08 ? 1 : 0;
		}
	}
} 

/* 任务 BEEP */
static void app_task_beep(void* pvParameters)
{
	char beep_sta=0x00;
	BaseType_t rt;
	for(;;)
	{	
		rt=xQueueReceive(g_queue_beep,&beep_sta,portMAX_DELAY);
		if(rt == pdTRUE)
			BEEP=beep_sta & 0x01 ? 1:0;
	}
}


/* 任务 RTC时钟 */  
static void app_task_rtc(void* pvParameters)
{
	uint8_t time_buf[16]={0};
	uint8_t data_buf[64]={0};	
	uint8_t week_day[1]={0};
	oled_t oled;
	EventBits_t	EventBit;
//	RTC_TimeTypeDef RTC_TimeStructure;
//	RTC_DateTypeDef RTC_DateStructure;	
	dgb_printf_safe("[app_task_rtc] create success\r\n");	

	for(;;)
	{		
		/* 等待事件组中的相应事件位，或同步 */
		EventBit=xEventGroupWaitBits((EventGroupHandle_t)g_event_group,
								(EventBits_t)EVENT_GROUP_RTC_WAKEUP,
								(BaseType_t)pdTRUE,		
								(BaseType_t)pdFALSE,  
								(TickType_t)portMAX_DELAY);  	

		if(EventBit & EVENT_GROUP_RTC_WAKEUP)
		{	
			/* RTC_GetTime，获取时间 */
			RTC_GetTime(RTC_Format_BCD, &RTC_TimeStructure); 
			/* 格式化字符串 */
			sprintf((char *)time_buf,"%02x:%02x:%02x",RTC_TimeStructure.RTC_Hours,RTC_TimeStructure.RTC_Minutes,RTC_TimeStructure.RTC_Seconds);
			
			/* oled显示时间 */
			oled.ctrl=OLED_CTRL_SHOW_STRING;
			oled.x=25;
			oled.y=0;
			oled.str=time_buf;
			oled.font_size=16;
			xQueueSend(g_queue_oled,&oled,100);	
									
			//获取日期
			RTC_GetDate(RTC_Format_BCD,&RTC_DateStructure);	
			sprintf((char *)data_buf,"20%02x-%02x-%02x",RTC_DateStructure.RTC_Year,RTC_DateStructure.RTC_Month,RTC_DateStructure.RTC_Date);
		
			/* oled显示日期 */
			oled.ctrl=OLED_CTRL_SHOW_STRING;
			oled.x=25;
			oled.y=3;
			oled.str=data_buf;
			oled.font_size=16;
			xQueueSend(g_queue_oled,&oled,100);
			
			//显示星期几
			oled.ctrl=OLED_CTRL_SHOW_CHINESE;
			oled.x=0;
			oled.y=5;
			oled.chinese=14;
			xQueueSend(g_queue_oled,&oled,1000);
			oled.x=16;
			oled.y=5;
			oled.chinese=15;
			xQueueSend(g_queue_oled,&oled,1000);
			sprintf((char *)week_day,"%01x",RTC_DateStructure.RTC_WeekDay);
			oled.ctrl=OLED_CTRL_SHOW_STRING;
			oled.x=36;
			oled.y=5;
			oled.str=week_day;
			oled.font_size=16;
			xQueueSend(g_queue_oled,&oled,100);
			
			//显示"菜单"
			oled.ctrl=OLED_CTRL_SHOW_CHINESE;	
			oled.x=96;
			oled.y=5;
			oled.chinese=12;
			xQueueSend(g_queue_oled,&oled,100);
			oled.x=112;
			oled.y=5;
			oled.chinese=13;
			xQueueSend(g_queue_oled,&oled,100);
		}
	}
}

/* 任务 KEY按键 */ 
static void app_task_key(void* pvParameters)
{
	int32_t i=0;        // 菜单图标数组中对应的不同数据详情页
	uint32_t n=1;  		// 0：在图标选择页；1：在主菜单/数据详情页（优化后语义）
	uint32_t flag=0;	// 记录返回层级（0：未返回；1：返回主菜单；2：返回锁屏）
	// 主菜单标志位（1=当前在主菜单，0=数据页/图标页）
	uint8_t is_main_menu = 1; // 初始状态是主菜单，所以默认1
	
	uint8_t fire_sta[32];
	uint8_t mq2_sta[32];
	uint8_t dht11_sta[5];
	
	uint32_t distance=0;
	
	EventBits_t uxBits;
	BaseType_t xret;
	oled_t oled;
	uint8_t sr04_buf[64]={0};
	uint8_t fire_buf[64]={0};
	uint8_t mq2_buf[64]={0};
	uint8_t humi_buf[64]={0};
	uint8_t temp_buf[64]={0};
	
	// 菜单图标数组（5个功能图标）
	const uint8_t *menu[]={
		SR04_ALARM_BMP,    // 陌生人靠近家门警示图片
		LM393_ALARM_BMP,   // 火警提示图标
		MQ2_ALARM_BMP,     // 气体警示图标
		HUMI_BMP,          // 湿度信息图标
		TEMP_BMP};         // 温度信息图标
	dgb_printf_safe("[app_task_key] create success\r\n");
	
	for(;;)
	{
		/* 等待事件组中的相应事件位，逻辑或等待事件 */
		uxBits=xEventGroupWaitBits(g_event_group,
									EVENT_GROUP_KEY1_DOWN|EVENT_GROUP_KEY2_DOWN|EVENT_GROUP_KEY3_DOWN|EVENT_GROUP_KEY4_DOWN,
									pdTRUE,pdFALSE,
									portMAX_DELAY);
			
		if(uxBits & EVENT_GROUP_KEY1_DOWN)	//按键1：菜单键（主→图标）/确认键（图标→数据）
		{
			/* 禁止EXTI0触发中断 */
			NVIC_DisableIRQ(EXTI0_IRQn);
			
			/* 延时消抖 */
			vTaskDelay(100);
			
			/* 确认是按下 */
			if(KEY1 == 0)
			{
				flag=0; // 按下按键1，重置返回层级计数（避免返回逻辑混乱）
				dgb_printf_safe("[app_task_key] S1 Press\r\n");
				
				// n=1：当前在【主菜单】→ 进入【图标选择页】
				if(n == 1 && is_main_menu == 1)
				{
					vTaskSuspend(app_task_rtc_handle);	// 暂停时间刷新，避免覆盖图标
					
					// 清屏，显示第一个图标（超声波）
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=menu[0]; // 显示第一个图标（超声波）
					xQueueSend(g_queue_oled,&oled,1000);
					
					n=0; // 标记当前在【图标选择页】
					i=0; // 重置图标索引为第一个
					is_main_menu = 0; // 离开主菜单，标志位置0
				}
				// n=0：当前在【图标选择页】→ 进入【数据详情页】
				else if(n == 0)
				{
					// 清屏，显示当前图标对应数据
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					
					switch(i)
					{
						case 0: // 超声波数据
							
							distance = g_alarm_distance; // 获取全局变量
							sprintf((char *)sr04_buf,"security distances:%dmm",(int)distance);
							oled.ctrl=OLED_CTRL_SHOW_STRING;
							oled.x=0;
							oled.y=3;
							oled.str=sr04_buf;
							oled.font_size=12;
							xQueueSend(g_queue_oled,&oled,100);	
							
							vTaskResume(app_task_sr04_handle);	
							break;
						case 1: // 火警状态
							xret=xQueueReceive(g_queue_fire, fire_sta, portMAX_DELAY);
							if(xret != pdTRUE)
								dgb_printf_safe("recv fail\r\n");
							
							sprintf((char *)fire_buf,"fire alarm:%s",fire_sta);
							oled.ctrl=OLED_CTRL_SHOW_STRING;
							oled.x=0;
							oled.y=3;
							oled.str=fire_buf;
							oled.font_size=12;
							xQueueSend(g_queue_oled,&oled,100);
							break;
						case 2: // 气体报警状态
							xret=xQueueReceive(g_queue_mq2,mq2_sta,portMAX_DELAY);
							if(xret != pdTRUE)
								dgb_printf_safe("recv fail\r\n");
							
							sprintf((char *)mq2_buf,"poision gas alarm:%s",mq2_sta);
							oled.ctrl=OLED_CTRL_SHOW_STRING;
							oled.x=0;
							oled.y=3;
							oled.str=mq2_buf;
							oled.font_size=12;
							xQueueSend(g_queue_oled,&oled,100);
							break;
						case 3: // 湿度
							xret=xQueueReceive(g_queue_dht11,dht11_sta,portMAX_DELAY);
							if(xret != pdTRUE)
								dgb_printf_safe("recv fail\r\n");
							
							sprintf((char *)humi_buf,"current humidity:%d.%d",dht11_sta[0],dht11_sta[1]);
							oled.ctrl=OLED_CTRL_SHOW_STRING;
							oled.x=0;
							oled.y=3;
							oled.str=humi_buf;
							oled.font_size=12;
							xQueueSend(g_queue_oled,&oled,100);
							break;
						case 4: // 温度
							xret=xQueueReceive(g_queue_dht11,dht11_sta,portMAX_DELAY);
							if(xret != pdTRUE)
								dgb_printf_safe("recv fail\r\n");
							
							sprintf((char *)temp_buf,"current temperature:%d.%d",dht11_sta[2],dht11_sta[3]);
							oled.ctrl=OLED_CTRL_SHOW_STRING;
							oled.x=0;
							oled.y=3;
							oled.str=temp_buf;
							oled.font_size=12;
							xQueueSend(g_queue_oled,&oled,100);
							break;
					}
					
					n=1; // 标记当前在【数据详情页】
					is_main_menu = 0; // 数据页，主菜单标志位置0
				}
			}
			
			/* 允许EXTI0触发中断 */
			NVIC_EnableIRQ(EXTI0_IRQn);	
		}
		
		if(uxBits & EVENT_GROUP_KEY2_DOWN)	//按键2：返回键（数据→图标→主→锁屏）
		{
			/* 禁止EXTI2触发中断 */
			NVIC_DisableIRQ(EXTI2_IRQn);
			
			/* 延时消抖 */
			vTaskDelay(100);
			
			if(KEY2 == 0)
			{
				dgb_printf_safe("[app_task_key] S2 Press\r\n");
				flag++; // 每按一次，返回层级+1
				
				// 场景1：当前在【数据详情页】（n=1，is_main_menu=0）→ 返回【图标选择页】
				if(n == 1 && flag == 1 && is_main_menu == 0)
				{
					/* 挂起超声波任务（只有超声波数据页i=0时执行） */
					if(i == 0 && app_task_sr04_handle != NULL)
					{
						eTaskState task_state = eTaskGetState(app_task_sr04_handle);
						if(task_state != eSuspended)
						{
							vTaskSuspend(app_task_sr04_handle);
						}
					}
					
					// 清屏，显示当前数据页对应的图标
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=menu[i]; // 显示超声波图标或其他对应图标
					xQueueSend(g_queue_oled,&oled,1000);
					
					n=0;    // 标记进入图标选择页
					flag=0; // 重置flag
					is_main_menu = 0; // 图标页，标志位置0
				}
				// 场景2：当前在【图标选择页】（n=0）→ 返回【主菜单】
				else if(n == 0 && flag == 1)
				{
					// 清屏，恢复RTC任务（显示时间）
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);	
					
					vTaskResume(app_task_rtc_handle);
					
					n=1; // 标记进入主菜单
					i=0; // 重置图标索引
					flag=0; // 重置flag
					is_main_menu = 1; // 进入主菜单，标志位置1
				}
				// 场景3：当前在【主菜单】（n=1，is_main_menu=1，flag=1）→ 第一次按，显示提示
				else if(n == 1 && flag == 1 && is_main_menu == 1)
				{
					// 暂停RTC任务，避免时间覆盖提示
					vTaskSuspend(app_task_rtc_handle);
					
					// 清屏（确保无残留）
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					vTaskDelay(100); // 等待清屏完成
					
					// 显示提示文字
					oled.ctrl=OLED_CTRL_SHOW_STRING;
					oled.x=16; // 居中显示
					oled.y=3; 
					oled.font_size=12;
					oled.str="Press again to lock";
					xQueueSend(g_queue_oled,&oled,2000);
				}
				// 场景4：当前在【主菜单】（n=1，is_main_menu=1，flag>=2）→ 第二次按，返回锁屏
				else if(n == 1 && flag >= 2 && is_main_menu == 1)
				{
					// 彻底清屏
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					vTaskDelay(100); // 确保清屏完成
					
					// 显示锁屏图标和欢迎文字
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;				
					oled.pic=LOCK_BMP;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_CHINESE;
					oled.x=48;
					oled.y=1;
					oled.chinese=0;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=1;
					oled.chinese=1;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=1;
					oled.chinese=2;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=1;
					oled.chinese=3;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=48;
					oled.y=3;
					oled.chinese=4;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=3;
					oled.chinese=5;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=3;
					oled.chinese=6;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=3;
					oled.chinese=7;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=48;
					oled.y=5;
					oled.chinese=8;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=64;
					oled.y=5;
					oled.chinese=9;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=80;
					oled.y=5;
					oled.chinese=10;
					xQueueSend(g_queue_oled,&oled,1000);
					oled.x=96;
					oled.y=5;
					oled.chinese=11;
					xQueueSend(g_queue_oled,&oled,1000);
					
					// 清除残留按键事件
					xEventGroupClearBits(g_event_group, EVENT_GROUP_KEY1_DOWN|EVENT_GROUP_KEY2_DOWN|EVENT_GROUP_KEY3_DOWN|EVENT_GROUP_KEY4_DOWN);
					
					// 恢复RFID，挂起其他任务
					vTaskResume(app_task_rfid_handle);	
					vTaskSuspend(app_task_rtc_handle);	
					vTaskSuspend(app_task_key_handle);	
					vTaskSuspend(app_task_sr04_handle);
					vTaskSuspend(app_task_aspro_handle);	
					vTaskSuspend(app_task_bluetooth_handle);	
					
					// 重置状态变量（下次解锁后从主菜单开始）
					flag=0;
					n=1;
					i=0;
					is_main_menu = 1; // 重置为主菜单标志位
				}
			}
			
			/* 允许EXTI2触发中断 */
			NVIC_EnableIRQ(EXTI2_IRQn);	
		}

		if(uxBits & EVENT_GROUP_KEY3_DOWN)	//按键3：上一个图标（仅在图标选择页生效）
		{
			/* 禁止EXTI3触发中断 */
			NVIC_DisableIRQ(EXTI3_IRQn);
			
			/* 延时消抖 */
			vTaskDelay(100);
			
			if(KEY3 == 0)
			{
				flag=0; // 按上下键，重置返回层级计数
				dgb_printf_safe("[app_task_key] S3 Press\r\n");
				
				/* 6. FreeRTOS任务逻辑：仅在图标选择页（n=0）时执行切换，避免主菜单/数据页误操作
				   原逻辑未判断n，主菜单下按上下键也会切换图标（不合理）
				*/
				if(n == 0) // 仅当前在【图标选择页】时，允许切换图标
				{
					// 清屏，切换到上一个图标（循环：0→4）
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;
					i = (i == 0) ? 4 : i-1; // 简化循环逻辑，替代原if-else
					oled.pic=menu[i];
					xQueueSend(g_queue_oled,&oled,1000);
				}
			}
			
			/* 允许EXTI3触发中断 */
			NVIC_EnableIRQ(EXTI3_IRQn);	
		}
		
		
		if(uxBits & EVENT_GROUP_KEY4_DOWN)	//按键4：下一个图标（仅在图标选择页生效）
		{
			/* 禁止EXTI4触发中断 */
			NVIC_DisableIRQ(EXTI4_IRQn);
			
			/* 延时消抖 */
			vTaskDelay(100);
			
			if(KEY4 == 0)
			{
				flag=0; // 按上下键，重置返回层级计数
				dgb_printf_safe("[app_task_key] S4 Press\r\n");
				
				/* 7. FreeRTOS任务逻辑：同按键3，仅在图标选择页（n=0）时执行切换
				   原逻辑未判断n，主菜单/数据页下按此键会误切换图标（不合理）
				*/
				if(n == 0) // 仅当前在【图标选择页】时，允许切换图标
				{
					// 清屏，切换到下一个图标（循环：4→0）
					oled.ctrl=OLED_CTRL_CLEAR;
					xQueueSend(g_queue_oled,&oled,1000);
					
					oled.ctrl=OLED_CTRL_SHOW_PICTURE;
					oled.x=0;
					oled.y=0;
					oled.pic_width=128;
					oled.pic_height=8;
					i = (i == 4) ? 0 : i+1; // 简化循环逻辑，替代原if-else（更简洁稳定）
					oled.pic=menu[i];
					xQueueSend(g_queue_oled,&oled,1000);
				}
			}
			/* 允许EXTI4触发中断 */
			NVIC_EnableIRQ(EXTI4_IRQn);	
		}
	}
}


/* 任务 超声波 */ 
static void app_task_sr04(void* pvParameters)
{	
	uint32_t distance=0;
	uint8_t led_sta=0x00;
	uint8_t beep_sta=0x00;

	dgb_printf_safe("[app_task_sr04] create success\r\n");
	for(;;)
	{
		distance=sr04_get_distance();  //获取障碍物距离
		
		if(distance == 0xFFFFFFFF)
			dgb_printf_safe("[app_task_sr04] get dinstance error\r\n");
		else if(distance>=20 && distance<=4000)
		{
			// 直接用全局变量判断，无需读EEPROM
			//dgb_printf_safe("测量距离：%dmm，报警距离：%dmm\r\n", distance, g_alarm_distance);
			
//			//发送消息，超时时间为1000个节拍
//			eeprom.ctrl=AT24C02_READ;
//			eeprom.addr=5;    // 明确要读的地址（报警距离存在地址5）
//			eeprom.size=1;    // 明确要读的长度（1字节）
//			xQueueSend(g_queue_eeprom,&eeprom,1000); // 发送读请求
//			vTaskDelay(50);
//			
//			//获取eeprom里的安全判断距离
//			xret=xQueueReceive(g_queue_eeprom,&eeprom,portMAX_DELAY);
//			if(xret != pdTRUE)
//			{
//				dgb_printf_safe("recv fail\r\n");
//				continue;
//			}
//				
			if(distance < g_alarm_distance)  // 测量距离 < 报警距离
			{
				led_sta=0x0B;	//LED2开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
									
				beep_sta=0x01;	//蜂鸣器开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
				
				dgb_printf_safe("security alarm on!!!\r\n");
			}
			else
			{
				led_sta=0x0F;	//全灭
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
									
				beep_sta=0x00;	//蜂鸣器关
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
			}
		}
		delay_ms(500);
	}
}


/* 任务 火焰传感器 */ 
//static void app_task_lm393(void* pvParameters)
//{
//	u32 n=0;
//	u32  adc_val;
//	uint8_t beep_sta=0x00;
//	uint8_t fire_sta[32];
//	uint8_t last_fire_status = 0; // 记录上一次状态（用于检测变化）
//	dgb_printf_safe("[app_task_lm393] create success\r\n");
//	for(;;)
//	{	
//		adc_val=ADC_GetConversionValue(ADC1);  //获取转换的值
//	
//		 // dgb_printf_safe("fire_adc_val=%d\r\n",adc_val);  // 调试用
//		if(n > 5)
//		{
//			// 正常情况下，火焰传感器在无火焰时 AO 输出电压较高（接近 3.3V，对应 ADC 值大），有火焰时电压降低（ADC 值减小）满量程（4095）
//			if(adc_val <= 1000)
//			{
//				FIRE_D0=0;		//火警灯开
//				beep_sta=0x01;	//蜂鸣器开
//				//发送消息，超时时间为1000个节拍
//				xQueueSend(g_queue_beep,&beep_sta,1000);
//				vTaskDelay(1000);
//				beep_sta=0x00;	//蜂鸣器关
//				//发送消息，超时时间为1000个节拍
//				xQueueSend(g_queue_beep,&beep_sta,1000);
//						
//				//发送火警状态
//				strcpy((char *)fire_sta,"danger!!!");
//				//发送消息，超时时间为1000个节拍
//				xQueueSend(g_queue_fire,fire_sta,1000);
//				
//				dgb_printf_safe("fire alarm on!!!\r\n");
//			}
//			else
//			{	
//				FIRE_D0=1;		//火警灯关
//				
//				strcpy((char *)fire_sta,"safe");
//				//发送消息，超时时间为1000个节拍
//				xQueueSend(g_queue_fire,fire_sta,1000);
//			}	
//		}
//		n++;
//		vTaskDelay(500);
//	}
//}

static void app_task_lm393(void* pvParameters)
{
    u32 n = 0;
    u32 adc_val;           // 保留原变量，不删除
    uint8_t beep_sta = 0x00;
    uint8_t fire_sta[32];
    uint8_t last_fire_status = 0; // 记录上一次状态（用于检测变化）

    dgb_printf_safe("[app_task_lm393] create success\r\n");

    for(;;)
    {
        // ===================== 只修改这里 =====================
        // adc_val = fire_get_adc(); // 原来的ADC获取（注释掉）
        // dgb_printf_safe("fire_adc_val=%d\r\n",adc_val);  // 原来的调试输出
        adc_val = fire_get_d0();    // 替换为读取PB7电平（0=有火，1=无火）
        dgb_printf_safe("fire_d0_val=%d\r\n",adc_val);  // 调试输出改为DO电平

        if(n > 5) // 跳过前5次采样（稳定传感器）
        {
            // 1. 计算当前状态（0=safe，1=danger）
            // ===================== 逻辑修改：DO=0=有火=危险 =====================
            uint8_t current_fire_status = (adc_val == 0) ? 1 : 0;

            // 2. 仅当状态变化时，才更新缓存并释放信号量
            if(current_fire_status != last_fire_status)
            {
                // 加互斥锁：保护全局变量读写
                xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);

                // 更新全局缓存
                g_fire_status = current_fire_status;
                last_fire_status = current_fire_status; // 更新历史状态

                // 释放互斥锁
                xSemaphoreGive(g_mutex_alarm);

                // 3. 释放信号量：同时通知蓝牙和语音任务（状态已更新）
                xSemaphoreGive(g_sem_fire_bt);    // 通知蓝牙
                xSemaphoreGive(g_sem_fire_asr);   // 通知语音

                dgb_printf_safe("火焰状态变化：%s\r\n", current_fire_status ? "danger" : "safe");
            }

            // 4. 原有硬件控制逻辑（LED和蜂鸣器）
            if(current_fire_status == 1) // 报警状态
            {
                FIRE_D0 = 0; // 火警灯开
                beep_sta = 0x01; // 蜂鸣器开
                xQueueSend(g_queue_beep, &beep_sta, 1000);
                vTaskDelay(1000);
                beep_sta = 0x00; // 蜂鸣器关
                xQueueSend(g_queue_beep, &beep_sta, 1000);

                strcpy((char *)fire_sta, "danger!!!");
								//发送消息，超时时间为1000个节拍
								xQueueSend(g_queue_fire,fire_sta,1000);
            }
            else // 安全状态
            {
                FIRE_D0 = 1; // 火警灯关
                strcpy((char *)fire_sta, "safe");
								//发送消息，超时时间为1000个节拍
								xQueueSend(g_queue_fire,fire_sta,1000);
            }

        }

        n++;
        vTaskDelay(500); // 500ms采样一次
    }
}


//static void app_task_mq2(void* pvParameters)
//{
//    u32 n = 0;
//    u32 adc_val;
//    uint8_t beep_sta = 0x00;
//    uint8_t mq2_sta[32];
//    uint8_t last_mq2_status = 0; // 记录上一次状态（用于检测变化）

//    dgb_printf_safe("[app_task_mq2] create success\r\n");

//    for(;;)
//    {
//        adc_val = mq2_get_adc(); // 获取ADC值
//				dgb_printf_safe("mq2_adc_val=%d\r\n",adc_val); // 调试
//			
//        if(n > 0) // 跳过第1次采样（稳定传感器）
//        {
//            // 1. 计算当前状态（0=safe，1=danger）
//            uint8_t current_mq2_status = (adc_val >= 2000) ? 1 : 0;

//            // 2. 仅当状态变化时，才更新缓存并释放信号量
//            if(current_mq2_status != last_mq2_status)
//            {
//                // 加互斥锁：保护全局变量读写
//                xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);

//                // 更新全局缓存
//                g_mq2_status = current_mq2_status;
//                last_mq2_status = current_mq2_status; // 更新历史状态

//                // 释放互斥锁
//                xSemaphoreGive(g_mutex_alarm);

//                // 3. 释放信号量：同时通知蓝牙和语音任务（状态已更新）
//                xSemaphoreGive(g_sem_mq2_bt);     // 通知蓝牙
//                xSemaphoreGive(g_sem_mq2_asr);    // 通知语音

//                dgb_printf_safe("烟雾状态变化：%s\r\n", current_mq2_status ? "danger" : "safe");
//            }

//            // 4. 原有硬件控制逻辑（蜂鸣器）
//            if(current_mq2_status == 1) // 报警状态
//            {
//                beep_sta = 0x01; // 蜂鸣器开
//                xQueueSend(g_queue_beep, &beep_sta, 1000);
//                vTaskDelay(1000);
//                beep_sta = 0x00; // 蜂鸣器关
//                xQueueSend(g_queue_beep, &beep_sta, 1000);

//                strcpy((char *)mq2_sta, "danger!!!");
//								//发送消息，超时时间为1000个节拍
//								xQueueSend(g_queue_mq2,mq2_sta,1000);
//            }
//            else // 安全状态
//            {
//                strcpy((char *)mq2_sta, "safe");
//								//发送消息，超时时间为1000个节拍
//								xQueueSend(g_queue_mq2,mq2_sta,1000);
//            }
//        }

//        n = 1; // 从第2次循环开始，n始终>0
//        vTaskDelay(500); // 500ms采样一次
//    }
//}

static void app_task_mq2(void* pvParameters)
{
    u32 n = 0;
    u32 adc_val;           // 保留原变量，不删除
    uint8_t beep_sta = 0x00;
    uint8_t mq2_sta[32];
    uint8_t last_mq2_status = 0; // 记录上一次状态（用于检测变化）

    dgb_printf_safe("[app_task_mq2] create success\r\n");

    // MQ2传感器初始化（DO模式）
    mq2_init();

    for(;;)
    {
        // ===================== 修改为读取DO电平 =====================
        // adc_val = mq2_get_adc(); // 原来的ADC获取（注释掉）
        // dgb_printf_safe("mq2_adc_val=%d\r\n",adc_val); // 原来的调试输出
        adc_val = mq2_get_do();    // 读取MQ2 DO电平（0=有烟，1=无烟）
        dgb_printf_safe("mq2_do_val=%d\r\n",adc_val);  // 调试输出改为DO电平

        if(n > 0) // 跳过第1次采样（稳定传感器）
        {
            // 1. 计算当前状态（0=safe，1=danger）
            // ===================== 修改为DO判断：0=有烟=危险 =====================
            uint8_t current_mq2_status = (adc_val == 0) ? 1 : 0;

            // 2. 仅当状态变化时，才更新缓存并释放信号量
            if(current_mq2_status != last_mq2_status)
            {
                // 加互斥锁：保护全局变量读写
                xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);

                // 更新全局缓存
                g_mq2_status = current_mq2_status;
                last_mq2_status = current_mq2_status; // 更新历史状态

                // 释放互斥锁
                xSemaphoreGive(g_mutex_alarm);

                // 3. 释放信号量：同时通知蓝牙和语音任务（状态已更新）
                xSemaphoreGive(g_sem_mq2_bt);     // 通知蓝牙
                xSemaphoreGive(g_sem_mq2_asr);    // 通知语音

                dgb_printf_safe("烟雾状态变化：%s\r\n", current_mq2_status ? "danger" : "safe");
            }

            // 4. 原有硬件控制逻辑（蜂鸣器）
            if(current_mq2_status == 1) // 报警状态
            {
                beep_sta = 0x01; // 蜂鸣器开
                xQueueSend(g_queue_beep, &beep_sta, 1000);
                vTaskDelay(1000);
                beep_sta = 0x00; // 蜂鸣器关
                xQueueSend(g_queue_beep, &beep_sta, 1000);

                strcpy((char *)mq2_sta, "danger!!!");
								//发送消息，超时时间为1000个节拍
								xQueueSend(g_queue_mq2,mq2_sta,1000);
            }
            else // 安全状态
            {
                strcpy((char *)mq2_sta, "safe");
								//发送消息，超时时间为1000个节拍
								xQueueSend(g_queue_mq2,mq2_sta,1000);
            }
        }

        n = 1; // 从第2次循环开始，n始终>0
        vTaskDelay(500); // 500ms采样一次
    }
}

/* 任务 温湿度传感器 */ 
static void app_task_dht11(void* pvParameters)
{
	int32_t rt;
	uint8_t dht11_sta[5];
	dgb_printf_safe("[app_task_dht11] create success\r\n");
	for(;;)
	{
		rt = dht11_get_msg(dht11_sta);
		
		if(rt == 0)
		{
			dgb_printf_safe("H:%d.%d T:%d.%d\r\n",dht11_sta[0],dht11_sta[1],dht11_sta[2],dht11_sta[3]);
			
			//发送消息，超时时间为1000个节拍
			xQueueSend(g_queue_dht11,dht11_sta,1000);
		}
		else
			printf("dht11 read error code %d\r\n",rt);
		
		vTaskDelay(6000);
	}
}


/* 任务 蓝牙 */ 
static void app_task_bluetooth(void* pvParameters)
{
	uint32_t i;
	uint8_t led_sta=0x00;
	uint8_t beep_sta=0x00;
	eeprom_t eeprom;
//	uint8_t fire_sta[32]={0};
//	uint8_t mq2_sta[32]={0};
	uint8_t dht11_sta[5]={0};
	uint8_t send_buf[128]={0};
	BaseType_t xret;
	char *p=NULL;   //用于strtok截取字符串
	uint32_t hours,minutes,seconds;
	uint32_t year,month,day,weekday;
	uint32_t distance;
	uint8_t distance_buf[1]={0};
	uint32_t carnum;
	uint8_t carnum_buf[5]={0};
	// 定义局部临时缓冲区
	uint8_t temp_card[5]={0};  
	
	// 1. 信号量列表改为动态赋值（解决初始化错误）
    SemaphoreHandle_t sem_list_bt[2]; // 仅声明数组，不初始化
    sem_list_bt[0] = g_sem_fire_bt;   // 动态赋值（运行时）
    sem_list_bt[1] = g_sem_mq2_bt;
	
	dgb_printf_safe("[app_task_bluetooth] create success\r\n");
	for(;;)
	{
		// 1. 阻塞等待信号量（同时等火焰/烟雾，超时100ms，兼顾蓝牙指令响应）
		BaseType_t fire_triggered = xSemaphoreTake(sem_list_bt[0], pdMS_TO_TICKS(50));
        BaseType_t mq2_triggered = xSemaphoreTake(sem_list_bt[1], pdMS_TO_TICKS(50));
		
		// 3. 火焰信号量触发
        if(fire_triggered == pdTRUE)
        {
            xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
            sprintf((char *)send_buf, "fire alarm:%s\r\n", g_fire_status ? "danger!!!" : "safe");
            xSemaphoreGive(g_mutex_alarm);
            //blue_send_str((char *)send_buf);
        }

        // 4. 烟雾信号量触发
        if(mq2_triggered == pdTRUE)
        {
            xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
            sprintf((char *)send_buf, "poision gas alarm:%s\r\n", g_mq2_status ? "danger!!!" : "safe");
            xSemaphoreGive(g_mutex_alarm);
            //blue_send_str((char *)send_buf);
        }
			

		if(g_usart3_rx_end)
		{
			dgb_printf_safe("%s\r\n",g_usart3_rx_buf);
			
			//查看安防状态
			if(strstr((char *)g_usart3_rx_buf,"homecheck"))
			{	
				//发送消息，超时时间为1000个节拍
				eeprom.ctrl=AT24C02_READ;
				xQueueSend(g_queue_eeprom,&eeprom,1000);
				vTaskDelay(50);
				
				//获取eeprom里的安全判断距离
				xret=xQueueReceive(g_queue_eeprom,&eeprom,portMAX_DELAY);
				if(xret != pdTRUE)
					dgb_printf_safe("recv fail\r\n");
				
//				//获取火警状态
//				xret=xQueueReceive(g_queue_fire,fire_sta,portMAX_DELAY);
//				if(xret != pdTRUE)
//					dgb_printf_safe("recv fail\r\n");
//				
//				//获取气体报警状态
//				xret=xQueueReceive(g_queue_mq2,mq2_sta,portMAX_DELAY);
//				if(xret != pdTRUE)
//					dgb_printf_safe("recv fail\r\n");
				
				//获取温湿度
				xret=xQueueReceive(g_queue_dht11,dht11_sta,portMAX_DELAY);
				if(xret != pdTRUE)
					dgb_printf_safe("recv fail\r\n");
				
	
//				//拼接所有安防状态信息
//				sprintf((char *)send_buf,
//						"security distances:%dmm\r\nfire alarm:%s\r\npoision gas alarm:%s\r\nH:%d.%d T:%d.%d\r\n",
//						(int)eeprom.data[5],fire_sta,mq2_sta,dht11_sta[0],dht11_sta[1],dht11_sta[2],dht11_sta[3]);
				// 加锁读全局报警信息
				xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
				// 拼接信息：用全局缓存的最新状态
				sprintf((char *)send_buf,
						"security distances:%dmm\r\nfire alarm:%s\r\npoision gas alarm:%s\r\nH:%d.%d T:%d.%d\r\n",
						(int)eeprom.data[5],
						g_fire_status ? "danger!!!" : "safe",  // 火焰最新状态
						g_mq2_status ? "danger!!!" : "safe",   // 烟雾最新状态
						dht11_sta[0],dht11_sta[1],dht11_sta[2],dht11_sta[3]);
				xSemaphoreGive(g_mutex_alarm);
				
				//发送到蓝牙串口3
				blue_send_str((char *)send_buf);
			}
			
			//设置系统日期
			if(strstr((char *)g_usart3_rx_buf,"DATE SET"))
			{
				//例如："DATE SET-2025-3-2-1"
				//提取第一个子串
				p=strtok((char *)g_usart3_rx_buf,"-");//p="DATE SET"
				
				//提取年份
				p=strtok(NULL,"-");	
				year = atoi(p);//2025年
				
				//提取月份
				p = strtok(NULL,"-");
				month = atoi(p);// 3月
				
				 //提取天数
				p = strtok(NULL,"-");
				day = atoi(p);// 2日

				//提取星期几
				p = strtok(NULL,"-");
				weekday = atoi(p); // 星期一
				
				RTC_DateStructure.RTC_Year = year-2000;
				RTC_DateStructure.RTC_Month = month;
				RTC_DateStructure.RTC_Date = day;
				RTC_DateStructure.RTC_WeekDay = weekday;
				
				RTC_SetDate(RTC_Format_BIN, &RTC_DateStructure);
			}
			
			//设置系统时间
			if(strstr((char *)g_usart3_rx_buf,"TIME SET"))
			{
				//列如：TIME SET-10-20-30
				p=strtok((char *)g_usart3_rx_buf,"-");
			
				//提取小时
				p=strtok(NULL,"-");
				hours = atoi(p);

				//提取分钟
				p=strtok(NULL,"-");
				minutes = atoi(p);

				//提取秒钟
				p=strtok(NULL,"-");
				seconds = atoi(p);
				
				RTC_TimeStructure.RTC_Hours   = hours;
				RTC_TimeStructure.RTC_Minutes = minutes;
				RTC_TimeStructure.RTC_Seconds = seconds;
				
				RTC_SetTime(RTC_Format_BIN, &RTC_TimeStructure); 
			}
			
			//修改超声波报警距离
			if(strstr((char *)g_usart3_rx_buf,"DISTANCE SET"))
			{
				//列如：DISTANCE SET-100
				p=strtok((char *)g_usart3_rx_buf,"-");
				
				//提取报警距离
				p=strtok(NULL,"-");
				distance = atoi(p);
				//转换成字符类型
				distance_buf[0]=(char)distance;
				dgb_printf_safe("set distance success: %02X\r\n",distance_buf[0]);
				
				// 同步更新全局变量（关键）
				g_alarm_distance = distance_buf[0];
				
				//发送写入eeprom的消息，超时时间为1000个节拍
				eeprom.ctrl=AT24C02_WRITE;
				eeprom.addr=5;				//写入距离地址为5
				eeprom.size=1;				//写入大小为1
				eeprom.data=distance_buf;	//写入的字符串
				xQueueSend(g_queue_eeprom,&eeprom,1000);
				
				/* 响两次 */
				beep_sta=0x01;	//蜂鸣器开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
				delay_ms(100);
				
				beep_sta=0x00;	//蜂鸣器关
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);	
				delay_ms(100);
				
				beep_sta=0x01;	//蜂鸣器开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
				delay_ms(100);
				
				beep_sta=0x00;	//蜂鸣器关
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);	
			}
			
			//修改安防系统默认卡ID
			if(strstr((char *)g_usart3_rx_buf,"CARDNUM SET"))
			{
				//列如：CARDNUM SET-214-56-243-5-24
				// 白卡：有效卡 {0x9C,0x4B,0x8E,0x05,0x5C} => 156-75-142-5-92
				// 蓝卡：无效卡 {0xD6 0x38 0xF3 0x05 0x18} => 214-56-243-5-24
				
				p=strtok((char *)g_usart3_rx_buf,"-");
				
				//提取5个卡号
				p=strtok(NULL,"-");
				carnum = atoi(p);
				//转换成字符类型
				carnum_buf[0]=(char)carnum;
				
				p=strtok(NULL,"-");
				carnum = atoi(p);
				//转换成字符类型
				carnum_buf[1]=(char)carnum;
				
				p=strtok(NULL,"-");
				carnum = atoi(p);
				//转换成字符类型
				carnum_buf[2]=(char)carnum;
				
				p=strtok(NULL,"-");
				carnum = atoi(p);
				//转换成字符类型
				carnum_buf[3]=(char)carnum;
				
				p=strtok(NULL,"-");
				carnum = atoi(p);
				//转换成字符类型
				carnum_buf[4]=(char)carnum;
				
				dgb_printf_safe("set carnum success: \r\n");
				for(i=0; i<5; i++)
					dgb_printf_safe("%02X ",carnum_buf[i]);
				dgb_printf_safe("\r\n");
				
				memcpy(temp_card, carnum_buf, 5);

				xSemaphoreTake(g_mutex_card, portMAX_DELAY);  // 获取互斥锁，阻塞等待
				// 赋值给全局数组
				for(i=0; i<5; i++)
				{
					g_valid_card_id[i] = temp_card[i];
				}
				xSemaphoreGive(g_mutex_card);  // 释放互斥锁
				
				//发送写入eeprom的消息，超时时间为1000个节拍
				eeprom.ctrl=AT24C02_WRITE;
				eeprom.addr=0;			//写入距离地址为0
				eeprom.size=5;			//写入大小为5
				eeprom.data=carnum_buf;	//写入的字符串
				xQueueSend(g_queue_eeprom,&eeprom,1000);
				
				/* 响两次 */
				beep_sta=0x01;	//蜂鸣器开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
				delay_ms(100);
				
				beep_sta=0x00;	//蜂鸣器关
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);	
				delay_ms(100);
				
				beep_sta=0x01;	//蜂鸣器开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);
				delay_ms(100);
				
				beep_sta=0x00;	//蜂鸣器关
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_beep,&beep_sta,1000);	
			}
			
			//打开客厅灯(LED3)
			if(strstr((char *)g_usart3_rx_buf,"led-on"))
			{
				led_sta=0x07;	//LED3开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
			}
			
			//关闭客厅灯(LED3)
			if(strstr((char *)g_usart3_rx_buf,"led-off"))
			{
				led_sta=0x0F;	//全灭
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
			}
			
			g_usart3_rx_end=0;
			g_usart3_rx_cnt=0;
			memset((void *)g_usart3_rx_buf,0,sizeof(g_usart3_rx_buf));
		}
	}
}


/* 任务 语音识别 */ 
static void app_task_aspro(void* pvParameters)
{
	uint8_t led_sta=0x00;
	uint8_t dht11_sta[5]={0};
//	uint8_t fire_sta[32]={0};
//	uint8_t mq2_sta[32]={0};
	
	char buf[32]={0};
	uint32_t value;
	EventBits_t	EventBit;
	BaseType_t xret;
	
	// 1. 信号量列表动态赋值
    SemaphoreHandle_t sem_list_asr[2];
    sem_list_asr[0] = g_sem_fire_asr;
    sem_list_asr[1] = g_sem_mq2_asr;
	
	dgb_printf_safe("[app_task_aspro] create success\r\n");
	
	for(;;)
	{
		// 2. 逐个等待信号量（各50ms，总超时100ms）
        BaseType_t fire_triggered = xSemaphoreTake(sem_list_asr[0], pdMS_TO_TICKS(50));
        BaseType_t mq2_triggered = xSemaphoreTake(sem_list_asr[1], pdMS_TO_TICKS(50));

        // 3. 火焰报警时播报
        if(fire_triggered == pdTRUE)
        {
            xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
            if(g_fire_status == 1)
            {
                asr_send_str("4#");
                dgb_printf_safe("语音播报：火焰报警！\r\n");
            }
            xSemaphoreGive(g_mutex_alarm);
        }

        // 4. 烟雾报警时播报
        if(mq2_triggered == pdTRUE)
        {
            xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
            if(g_mq2_status == 1)
            {
                asr_send_str("6#");
                dgb_printf_safe("语音播报：烟雾报警！\r\n");
            }
            xSemaphoreGive(g_mutex_alarm);
        }
		
		if(g_usart2_rx_end) 
		{
			dgb_printf_safe("%s\r\n",g_usart2_rx_buf);
			
			//打开客厅灯
			if(strstr((char *)g_usart2_rx_buf,"LED ON"))
			{
				led_sta=0x07;	//LED3开
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
				asr_send_str("1#");
			}
			
			//关闭客厅灯
			if(strstr((char *)g_usart2_rx_buf,"LED OFF"))
			{
				led_sta=0x0F;	//全灭
				//发送消息，超时时间为1000个节拍
				xQueueSend(g_queue_led,&led_sta,1000);
				asr_send_str("2#");
			}
			
			//播报当前时间
			if(strstr((char *)g_usart2_rx_buf,"TIME"))
			{
				/* 挂起rtc任务 */
				vTaskSuspend(app_task_rtc_handle);	

				/* 等待事件组中的相应事件位，或同步 */
				EventBit=xEventGroupWaitBits((EventGroupHandle_t)g_event_group,
										(EventBits_t)EVENT_GROUP_RTC_WAKEUP,
										(BaseType_t)pdTRUE,		
										(BaseType_t)pdFALSE,  
										(TickType_t)portMAX_DELAY);  	

				if(EventBit & EVENT_GROUP_RTC_WAKEUP)
				{
					// dgb_printf_safe("等待RTC唤醒事件成功\r\n");  // 添加事件等待确认调试
					/* RTC_GetTime，获取时间 */
					RTC_GetTime(RTC_Format_BIN, &RTC_TimeStructure); 
					value=(RTC_TimeStructure.RTC_Hours<<16) \
						|(RTC_TimeStructure.RTC_Minutes<<8) \
						|(RTC_TimeStructure.RTC_Seconds);
					
					dgb_printf_safe("time_value=%d\r\n",value);
					sprintf(buf,"%d#",value);
					//发送时间给到语音模块播报
					asr_send_str(buf);
					// dgb_printf_safe("时间数据已发送\r\n");  // 添加发送确认调试
				}
				
				/* 恢复rtc任务 */
				vTaskResume(app_task_rtc_handle);
			}
			
			//播报当前日期
			if(strstr((const char *)g_usart2_rx_buf,"DATE"))
			{
				RTC_GetDate(RTC_Format_BIN, &RTC_DateStructure);	
				/*  bit[0 - 3]: RTC_WeekDay 1~7
					bit[4 - 8]: RTC_Date 1~31
					bit[9 - 12]: RTC_Month 1~12
					bit[13 - 23]: RTC_Year 0~99
					bit[24 - 31]: reserve
				*/
				value=(RTC_DateStructure.RTC_Year<<13) \
					|(RTC_DateStructure.RTC_Month<<9) \
					|(RTC_DateStructure.RTC_Date<<4)\
					|(RTC_DateStructure.RTC_WeekDay);
				
				dgb_printf_safe("date_value=%d\r\n",value);
				sprintf(buf,"%d#",value);
				
				asr_send_str(buf);
			
			}
			
			//播报温度
			if(strstr((char *)g_usart2_rx_buf,"TEMP"))
			{
				//获取温度
				xret=xQueueReceive(g_queue_dht11,(uint8_t *)dht11_sta,portMAX_DELAY);
				if(xret != pdTRUE)
					dgb_printf_safe("recv fail\r\n");
				
				dgb_printf_safe("T:%d.%d\r\n",dht11_sta[2],dht11_sta[3]);
				
				sprintf(buf,"%d#",dht11_sta[2]);
				
				//发送温度给到语音模块播报
				asr_send_str(buf);
			}
			
			//播报湿度
			if(strstr((char *)g_usart2_rx_buf,"HUMI"))
			{
				//获取湿度
				xret=xQueueReceive(g_queue_dht11,(uint8_t *)dht11_sta,portMAX_DELAY);
				if(xret != pdTRUE)
					dgb_printf_safe("recv fail\r\n");
				
				dgb_printf_safe("H:%d.%d\r\n",dht11_sta[0],dht11_sta[1]);
				
				sprintf(buf,"%d#",dht11_sta[0]);
				
				//发送湿度给到语音模块播报
				asr_send_str(buf);
			}
			
//			//播报火警状态
//			if(strstr((char *)g_usart2_rx_buf,"FIRE"))
//			{		
//				//获取火警状态
//				xret=xQueueReceive(g_queue_fire,fire_sta,portMAX_DELAY);
//				if(xret != pdTRUE)
//					dgb_printf_safe("recv fail\r\n");
//				
//				//测试
//				dgb_printf_safe("%s\r\n",fire_sta);
//				
//				//判断状态
//				if(strstr((char *)fire_sta,"safe"))
//					sprintf(buf,"3#");  // 火焰安全
//				else
//					sprintf(buf,"4#");  // 火焰不安全
//					
//				//发送火警状态给到语音模块播报
//				asr_send_str(buf);
//			}

			// 查询火警状态：读全局缓存
			if(strstr((char *)g_usart2_rx_buf,"FIRE"))
			{		
				xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
				asr_send_str(g_fire_status ? "4#" : "3#"); // 1=danger→4#，0=safe→3#
				xSemaphoreGive(g_mutex_alarm);
			}
			
//			//播报烟雾报警状态
//			if(strstr((char *)g_usart2_rx_buf,"GAS"))
//			{
//				
//				//获取气体报警状态
//				xret=xQueueReceive(g_queue_mq2,mq2_sta,portMAX_DELAY);
//				if(xret != pdTRUE)
//					dgb_printf_safe("recv fail\r\n");
//				
//				//测试
//				dgb_printf_safe("%s\r\n",mq2_sta);
//				
//				//判断状态
//				if(strstr((char *)mq2_sta,"safe"))
//					sprintf(buf,"5#");  // 烟雾安全→5#
//				else
//					sprintf(buf,"6#");  // 烟雾不安全→6#
//				
//				//发送气体报警状态给到语音模块播报
//				asr_send_str(buf);
//				
//			}

			// 查询烟雾状态：读全局缓存
			if(strstr((char *)g_usart2_rx_buf,"GAS"))
			{
				xSemaphoreTake(g_mutex_alarm, portMAX_DELAY);
				asr_send_str(g_mq2_status ? "6#" : "5#"); // 1=danger→6#，0=safe→5#
				xSemaphoreGive(g_mutex_alarm);
			}
			
			g_usart2_rx_end=0;
			g_usart2_rx_cnt=0;
			memset((void *)g_usart2_rx_buf,0,sizeof(g_usart2_rx_buf));
		}
	}
}


/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* vApplicationMallocFailedHook() will only be called if
	configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
	function that will get called if a call to pvPortMalloc() fails.
	pvPortMalloc() is called internally by the kernel whenever a task, queue,
	timer or semaphore is created.  It is also called by various parts of the
	demo application.  If heap_1.c or heap_2.c are used, then the size of the
	heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
	FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
	to query the size of free heap space that remains (although it does not
	provide information on how the remaining heap might be fragmented). */
	taskDISABLE_INTERRUPTS();
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
	/* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
	to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
	task.  It is essential that code added to this hook function never attempts
	to block in any way (for example, call xQueueReceive() with a block time
	specified, or call vTaskDelay()).  If the application makes use of the
	vTaskDelete() API function (as this demo application does) then it is also
	important that vApplicationIdleHook() is permitted to return to its calling
	function, because it is the responsibility of the idle task to clean up
	memory allocated by the kernel to any task that has since been deleted. */
	
	/* 进入睡眠模式 */
	__WFI();	
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	taskDISABLE_INTERRUPTS();
	for( ;; );
}


void vApplicationTickHook( void )
{

}
