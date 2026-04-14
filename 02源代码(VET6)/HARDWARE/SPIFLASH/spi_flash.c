#include "spi_flash.h"

//w25q128初始化(SPI FLASH)
void w25q128_init(void)
{
//	//使能spi时钟 
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
	//使能端口B时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
	
	//配置PB4为输入模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_4;//4号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN;//输入模式（引脚既可以软件代码控制，也可以其他外设控制）
	GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_High_Speed;//高速，功耗高，但是引脚响应时间更短
	GPIO_Init(GPIOB,&GPIO_InitStructure);	
	
//	//将PB3~PB5连接到SPI1硬件
//	GPIO_PinAFConfig(GPIOB, GPIO_PinSource3, GPIO_AF_SPI1);
//	GPIO_PinAFConfig(GPIOB, GPIO_PinSource4, GPIO_AF_SPI1);
//	GPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_SPI1);
	
	//PB3 PB5 PB14配置输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_3|GPIO_Pin_5|GPIO_Pin_14;//3 5 14号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT;//输出功能模式（引脚既可以软件代码控制，也可以其他外设控制）
	GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;//推输出挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_High_Speed;//高速，功耗高，但是引脚响应时间更短
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
	//PB14肯定有初始电平状态，看时序图
	W25Q128_CS=1;
	W25Q128_SCLK=1;
	W25Q128_MOSI=1;//任意电平值都可以
	
//	//配置SPI1相关参数(使用模式3)，看库函数帮助文档
//	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;//双线全双工
//	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;//M4工作在主机角色
//	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;//看时序图，以最小的字节单元
//	SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;//看时序图，CPOL=1
//	SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;//看时序图,CPHA=1 (第二边沿采集数据)
//	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;//看时序图，通过软件代码控制
//	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;//看从机的数据手册，不能超速，SCLK的频率=84MHz/16=5.25MHz
//	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;//看从机的数据手册
//	SPI_Init(SPI1, &SPI_InitStructure);
	
//	//使能SPI1工作
//	SPI_Cmd(SPI1,ENABLE);
}


//模拟api
//发送字节给从机，返回采集的数据
uint8_t SPI1_SendByte(uint8_t byte)
{
	int32_t i=0;
	uint8_t d=0;
	for(i=7; i>=0; i--)
	{
		//发送MOSI
		if(byte & (1<<i))
			W25Q128_MOSI=1;
		else
			W25Q128_MOSI=0;
	
		W25Q128_SCLK=0;
		delay_us(1);
		
		W25Q128_SCLK=1;
		delay_us(1);

		//获取到MISO数据
		if(W25Q128_MISO)
			d|=1<<i;
	}
	return d;
}


//读取数据
void w25q128_read_data(uint32_t addr,uint8_t *buf,uint32_t len)
{
	uint8_t *p=buf;
	
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;
	
	//发送0x03
	SPI1_SendByte(0x03);
	
	//发送24bit地址
	SPI1_SendByte((addr>>16)&0xFF);
	SPI1_SendByte((addr>>8)&0xFF);
	SPI1_SendByte(addr&0xFF);
	
	while(len--)
	{
		//得到厂商ID，参数为任意值
		*p=SPI1_SendByte(0xFF);
		
		p++;
	}
	
	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;
}


//sflash的写使能(相当于flash的解锁)
void w25q128_write_enable(void)
{
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;

	//发送0x06指令，写使能
	SPI1_SendByte(0x06);	
	
	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;	
}


//sflash的写失能(相当于flash的上锁)
void w25q128_write_disable(void)
{
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;
	
	//发送0x04指令，写失能
	SPI1_SendByte(0x04);

	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;	
}


//读取statu1
uint8_t w25q128_read_status1(void)
{
	uint8_t sta;
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;
	
	//发送0x05指令，读取状态寄存器1
	SPI1_SendByte(0x05);
	
	sta=SPI1_SendByte(0xFF);

	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;	
	
	return sta;
}


//扇区擦除
void w25q128_sector_erase(uint32_t sector_addr)
{
	uint8_t sta;
	w25q128_write_enable();  //解锁
	
	delay_us(1);
	
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;
	
	//发送0x20，扇区擦除指令
	SPI1_SendByte(0x20);

	//发送24bit地址
	SPI1_SendByte((sector_addr>>16)&0xFF);
	SPI1_SendByte((sector_addr>>8)&0xFF);
	SPI1_SendByte(sector_addr&0xFF);

	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;

	delay_us(1);
	
	while(1)
	{
		sta=w25q128_read_status1();
		
		//检查状态寄存器1中的BUSY位是否由1->0
		if((sta & 0x01)==0x00)
		{
			break;
		}
		
		delay_ms(1);
	}
	
	w25q128_write_disable();  //上锁
}


//页编程
void w25q128_page_program(uint32_t addr,uint8_t *buf,uint32_t len)
{
	uint8_t sta;
	uint8_t *p=buf;
	
	w25q128_write_enable();  //解锁
	
	delay_us(1);
	
	//CS引脚为低电平，从机开始工作
	W25Q128_CS=0;
	
	//发送0x02，页编程指令
	SPI1_SendByte(0x02);

	//发送24bit地址
	SPI1_SendByte((addr>>16)&0xFF);
	SPI1_SendByte((addr>>8)&0xFF);
	SPI1_SendByte(addr&0xFF);
	
	//循环写入多个字节
	len = len>256?256:len;
	
	while(len--)
	{
		SPI1_SendByte(*p);
		
		p++;
	
	}

	//CS引脚为高电平，从机停止工作
	W25Q128_CS=1;

	delay_us(1);
	
	while(1)
	{
		sta=w25q128_read_status1();
		
		//检查状态寄存器1中的BUSY位是否由1->0
		if((sta & 0x01)==0x00)
		{
			break;
		}
		
		delay_ms(1);
	}
	
	w25q128_write_disable();  //上锁
}
