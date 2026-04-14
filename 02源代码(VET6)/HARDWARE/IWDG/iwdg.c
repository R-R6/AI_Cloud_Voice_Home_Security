#include "includes.h"

//独立看门狗初始化
void iwdg_init(void)
{
	//解锁独立看门狗的寄存器
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
		
	//进行32分频，独立看门狗的硬件时钟频率=32KHz/256=125Hz
	//就是独立看门狗进行125次计数，就是1秒时间的到达
	IWDG_SetPrescaler(IWDG_Prescaler_256);

	//设置独立看门狗的计数值，如果要超时时间为5秒，这里计数值填写为625
	IWDG_SetReload(625-1);
	
	//刷新计数值
	IWDG_ReloadCounter();
	
	//使能独立的看门狗
	IWDG_Enable();
}
