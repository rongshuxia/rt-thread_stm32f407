#include "stm32f4xx.h"


__asm void SysCtlDelay(uint16_t ulCount)
{
    subs    r0, #1;
    bne     SysCtlDelay;
    bx      lr;
}

void stm32f4_delay_us(uint16_t us)
{
    SysCtlDelay(56 * us);
}

void stm32f4_delay_ms(uint16_t ms)
{
	int i;
    
	for(i = 0; i < ms; i++)
    {
        stm32f4_delay_us(1000);
    }
}
