#include <stdint.h>
#include <rthw.h>
#include <rtthread.h>
#include <stdio.h>
#include "regdef.h"
#include "common_func.h"
#include "confreg_time.h"
#include "led.h"

void Timer_IntrHandler(void);
void Button_IntrHandler(unsigned char button_state);

static uint32_t _SysTick_Config(rt_uint32_t ticks)
{
    RegWrite(0xbf20f004,0x0f);//edge
	RegWrite(0xbf20f008,0x1f);//pol
	RegWrite(0xbf20f00c,0x1f);//clr
	RegWrite(0xbf20f000,0x1f);//en

	RegWrite(0xbf20f104,ticks);//timercmp 1ms:500000
	RegWrite(0xbf20f108,0x1);//timeren
    
    return 0;
}


extern char __heap_start[];
extern char __heap_end[];

static void *heap_start = __heap_start;
static void *heap_end = __heap_end;

/**
 * This function will initial your board.
 */
void rt_hw_board_init()
{
    /* System Tick Configuration */
    _SysTick_Config(CONFREG_CLOCKS_PER_SEC / RT_TICK_PER_SECOND);

    /* Call components board initial (use INIT_BOARD_EXPORT()) */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
    rt_system_heap_init((void *)heap_start, (void *)heap_end);
#endif
}

void HWI0_IntrHandler(void)
{	
	unsigned int int_state;
	int_state = RegRead(0xbf20f014);

	if((int_state & 0x10) == 0x10){
		Timer_IntrHandler();
	}
	else if(int_state & 0xf){
		Button_IntrHandler(int_state & 0xf);
	}
}

void Timer_IntrHandler(void)
{
	RegWrite(0xbf20f108,0);
	RegWrite(0xbf20f108,1);
    rt_tick_increase();
}

void Button_IntrHandler(unsigned char button_state)
{
	if((button_state & 0b1000) == 0b1000){
		toggleLedPin(15);
		RegWrite(0xbf20f00c,0x8);//clr
	}
	else if((button_state & 0b0100) == 0b0100){
		toggleLedPin(14);
		RegWrite(0xbf20f00c,0x4);//clr
	}
	else if((button_state & 0b0010) == 0b0010){
		toggleLedPin(13);
		RegWrite(0xbf20f00c,0x2);//clr
	}
	else if((button_state & 0b0001) == 0b0001){
		toggleLedPin(12);
		RegWrite(0xbf20f00c,0x1);//clr
	}
}

extern void uart_putchar(char c);

/**
* @brief  重映射串口到rt_kprintf()函数
* @param  str：要输出到串口的字符串
* @retval 无
*
* @attention
*
*/
void rt_hw_console_output(const char *str)
{
    /* 进入临界段 */
    rt_enter_critical();

    /* 直到字符串结束 */
    while (*str!='\0')
    {
		if (*str=='\n') uart_putchar('\r');
		uart_putchar(*str++);
    }

    /* 退出临界段 */
    rt_exit_critical();
}

extern unsigned long UART_BASE;

char rt_hw_console_getchar(void)
{
	char ch = -1;

    if ((*( volatile char * ) ( UART_BASE + 0x5 )) & 0x1)
    {
        ch = *((volatile unsigned char *)(UART_BASE));
    }
	else
	{
		rt_thread_mdelay(10);
	}

	return ch;
}
