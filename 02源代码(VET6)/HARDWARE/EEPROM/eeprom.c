#include "includes.h"

/*
    SCL   接PB8（SCL）
    SDA   接PB9（SDA） 
*/
#define EEPROM_SCL_W	PBout(8)
#define EEPROM_SDA_W	PBout(9)
#define EEPROM_SDA_R	PBin(9)

static GPIO_InitTypeDef GPIO_InitStructure;

//eeprom初始化
void at24c02_init(void)
{
	//打开端口B的电源供电,就是使能该端口的硬件时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
	
	//配置端口B的 8 9号引脚，配置为推挽输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_8|GPIO_Pin_9;//8 9号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT;//输出模式
	GPIO_InitStructure.GPIO_OType=GPIO_OType_OD;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_Low_Speed;//低速，功耗低，但是引脚响应时间更长
	GPIO_Init(GPIOB,&GPIO_InitStructure);

	//看时序图
	EEPROM_SCL_W=1;
	EEPROM_SDA_W=1;
}


//更改sda引脚的模式
static void sda_pin_mode( GPIOMode_TypeDef pin_mode)
{
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9;//9号引脚
	GPIO_InitStructure.GPIO_Mode=pin_mode;//输入模式
	GPIO_InitStructure.GPIO_OType=GPIO_OType_OD;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_Low_Speed;//低速，功耗低，但是引脚响应时间更长
	GPIO_Init(GPIOB,&GPIO_InitStructure);
}


//sda启动命令
void i2c_start(void)
{
	sda_pin_mode(GPIO_Mode_OUT);
	
	EEPROM_SCL_W=1;
	EEPROM_SDA_W=1;
	delay_us(5);
	
	EEPROM_SDA_W=0;	
	delay_us(5);

	EEPROM_SCL_W=0;	
	delay_us(5);
}


//sda停止命令
void i2c_stop(void)
{
	sda_pin_mode(GPIO_Mode_OUT);	
	EEPROM_SCL_W=1;
	EEPROM_SDA_W=0;
	delay_us(5);
	
	EEPROM_SDA_W=1;	
	delay_us(5);	
}


//发送字节
void i2c_send_byte(uint8_t byte)
{
	int32_t i;
	sda_pin_mode(GPIO_Mode_OUT);
	EEPROM_SCL_W=0;
	EEPROM_SDA_W=0;
	delay_us(5);
	
	for(i=7; i>=0; i--)
	{
		if(byte & (1<<i))
			EEPROM_SDA_W=1;
		else
			EEPROM_SDA_W=0;
	
		delay_us(5);
		
		//当前SDA引脚电平是不变的，然后从机可以可靠访问
		EEPROM_SCL_W=1;	
		delay_us(5);

		//当前SDA引脚电平可能会发生变更，然后从机访问是不可靠
		EEPROM_SCL_W=0;	
		delay_us(5);
	}
}


//等待应答信号
uint8_t i2c_wait_ack(void)
{
	uint8_t ack=0;
	sda_pin_mode(GPIO_Mode_IN);	
	
	EEPROM_SCL_W=1;
	delay_us(5);
	
	//检测到SDA引脚为低电平，就是从机有应答
	if(EEPROM_SDA_R==0)
		ack=0;
	else
		ack=1;
	
	EEPROM_SCL_W=0;
	delay_us(5);	

	return ack;
}


//写入数据
int32_t at24c02_write(uint8_t word_addr,uint8_t *buf,uint32_t len)
{
	uint8_t ack;
	uint8_t *p=buf;
	
	//发送起始信号
	i2c_start();
	
	//寻址
	i2c_send_byte(0xA0);
	
	//等待从机的应答信号
	ack=i2c_wait_ack();
	if(ack)
	{
		printf("device address fail\r\n");
		return -1;
	}

	//告诉从机往它芯片哪个存储地址写入数据
	i2c_send_byte(word_addr);
	
	//等待从机的应答信号
	ack=i2c_wait_ack();
	if(ack)
	{
		printf("word address fail\r\n");
		return -2;
	}
	
	//连续写入多个字节
	while(len--)
	{
		//写入字节
		i2c_send_byte(*p);
		
		//等待从机的应答信号
		ack=i2c_wait_ack();
		if(ack)
		{
			printf("write data fail\r\n");
			return -3;
		}
		
		p++;
	}

	i2c_stop();
	
	return 0;
}


//主机发送应答信号
void i2c_send_ack(uint8_t ack)
{
	sda_pin_mode(GPIO_Mode_OUT);
	EEPROM_SCL_W=0;
	EEPROM_SDA_W=0;
	delay_us(5);
	
	if(ack)
		EEPROM_SDA_W=1;
	else
		EEPROM_SDA_W=0;
	
	delay_us(5);
	
	EEPROM_SCL_W=1;	
	delay_us(5);
	
	EEPROM_SCL_W=0;
	delay_us(5);
}


//接收字节
uint8_t i2c_recv_byte(void)
{
	int32_t i;
	uint8_t d=0;
	
	sda_pin_mode(GPIO_Mode_IN);
	
	for(i=7; i>=0; i--)
	{
		EEPROM_SCL_W=1;
		delay_us(5);
		
		if(EEPROM_SDA_R)
			d|=1<<i;
		
		EEPROM_SCL_W=0;
		delay_us(5);
	}
	return d;
}


//读取数据
int32_t  at24c02_read(uint8_t word_addr,uint8_t *buf,uint32_t len)
{
	uint8_t ack;
	uint8_t *p=buf;

	//发送起始信号
	i2c_start();
	
	//寻址
	i2c_send_byte(0xA0);
		
	//等待从机的应答信号
	ack=i2c_wait_ack();
	if(ack)
	{
		printf("device address fail\r\n");
		return -1;
	}
	
	//告诉从机往它芯片哪个存储地址写入数据
	i2c_send_byte(word_addr);
	
	//等待从机的应答信号
	ack=i2c_wait_ack();
	if(ack)
	{
		printf("word address fail\r\n");
		return -2;
	}
	
	//重新发送起始信号
	i2c_start();
	
	//寻址
	i2c_send_byte(0xA1);
	
	//等待从机的应答信号
	ack=i2c_wait_ack();
	if(ack)
	{
		printf("device address fail with read\r\n");
		return -3;
	}	
	
	len=len-1;
	
	//连续读取多个字节
	while(len--)
	{
		//读取字节
		*p=i2c_recv_byte();
		
		//主动发送应答信号，告诉从机，当前主机成功接收到字节
		i2c_send_ack(0);
		
		p++;
	}
	
	//读取最后一个字节,后面紧接着无应答信号
	*p=i2c_recv_byte();	
	
	//发送无应答信号
	i2c_send_ack(1);

	i2c_stop();
	
	return 0;
}

