#ifndef __ASPRO_H
#define __ASPRO_H

/*
	GND   电源地
    VCC   接5V电源
    TX    接PA2
    RX    接PA3     
*/

extern void asr_init(uint32_t baud);
extern void asr_send_str(char * str);

#endif
