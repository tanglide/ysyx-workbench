#include <stdio.h> 
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "common_func.h"

//BSP板级支持包所需全局变量
unsigned long UART_BASE = 0xbf000000;					//UART16550的虚地址
unsigned long CONFREG_TIMER_BASE = 0xbf20f100;			//CONFREG计数器的虚地址
unsigned long CONFREG_CLOCKS_PER_SEC = 50000000L;		//CONFREG时钟频率
unsigned long CORE_CLOCKS_PER_SEC = 33000000L;			//处理器核时钟频率

int  simu_flag;

void Timer_IntrHandler(void);
void Button_IntrHandler(unsigned char button_state);

int main(int argc, char** argv)
{	
	simu_flag = RegRead(0xbf20f500);

	RegWrite(0xbf20f004,0x0f);//edge
	RegWrite(0xbf20f008,0x1f);//pol
	RegWrite(0xbf20f00c,0x1f);//clr
	RegWrite(0xbf20f000,0x1f);//en

	if(simu_flag){
		RegWrite(0xbf20f104,50000);//timercmp 1ms
	}
	else{
		RegWrite(0xbf20f104,50000000);//timercmp 1s
	}
	RegWrite(0xbf20f108,0x1);//timeren

	while(1)
	{
		
	}

	return 0;
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
	RegWrite(0xbf20f108,0);//timeren
	RegWrite(0xbf20f108,1);//timeren
	printf("timer int\n");
}

void Button_IntrHandler(unsigned char button_state)
{
	if((button_state & 0b1000) == 0b1000){
		printf("button4 int\n");
		RegWrite(0xbf20f00c,0x8);//clr
	}
	else if((button_state & 0b0100) == 0b0100){
		printf("button3 int\n");
		RegWrite(0xbf20f00c,0x4);//clr
	}
	else if((button_state & 0b0010) == 0b0010){
		printf("button2 int\n");
		RegWrite(0xbf20f00c,0x2);//clr
	}
	else if((button_state & 0b0001) == 0b0001){
		printf("button1 int\n");
		RegWrite(0xbf20f00c,0x1);//clr
	}
}