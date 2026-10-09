#include "sys.h"
#include "delay.h"
#include "usart.h" 
#include "led.h"
#include "key.h"
#include "lcd.h"
#include "sdram.h"
/************************************************
 ALIENTEK 阿波罗STM32H7开发板 实验15
 SDRAM实验-HAL库函数版
 技术支持：www.openedv.com
 淘宝店铺：http://eboard.taobao.com 
 关注微信公众平台微信号："正点原子"，免费获取STM32资料。
 广州市星翼电子科技有限公司  
 作者：正点原子 @ALIENTEK
************************************************/

u16 testsram[250000] __attribute__((at(0XC0000000)));//测试用数组

//SDRAM内存测试	    
void fsmc_sdram_test(u16 x,u16 y)
{  
	vu32 i=0;  	  
	vu32 temp=0;	   
	vu32 sval=0;	//在地址0读到的数据	  				   
  	LCD_ShowString(x,y,180,y+16,16,"Ex Memory Test:    0KB "); 
	//每隔16K字节,写入一个数据,总共写入2048个数据,刚好是32M字节
	for(i=0;i<32*1024*1024;i+=16*1024)
	{
		*(vu32*)(Bank5_SDRAM_ADDR+i)=temp; 
		temp++;
	}
	//依次读出之前写入的数据,进行校验		  
 	for(i=0;i<32*1024*1024;i+=16*1024) 
	{	
  		temp=*(vu32*)(Bank5_SDRAM_ADDR+i);
		if(i==0)sval=temp;
 		else if(temp<=sval)break;//后面读出的数据一定要比第一次读到的数据大.	   		   
		LCD_ShowxNum(x+15*8,y,(u16)(temp-sval+1)*16,5,16,0);	//显示内存容量  
		printf("SDRAM Capacity:%dKB\r\n",(u16)(temp-sval+1)*16);//打印SDRAM容量
 	}					 
}

int main(void)
{	 
	u8 key;		 
 	u8 i=0;	     
	u32 ts=0; 
	
	Cache_Enable();                			//打开L1-Cache
	HAL_Init();				        		//初始化HAL库
	Stm32_Clock_Init(160,5,2,4);  		    //设置时钟,400Mhz 
	delay_init(400);						//延时初始化
	uart_init(115200);						//串口初始化
	LED_Init();								//初始化LED
	KEY_Init();								//初始化按键
	SDRAM_Init();                   		//初始化SDRAM
    LCD_Init();                     		//初始化LCD
    POINT_COLOR=RED;                		//设置字体为红色
    LCD_ShowString(30,50,200,16,16,"APOLLO STM32H7");	
	LCD_ShowString(30,70,200,16,16,"SDRAM TEST");	
	LCD_ShowString(30,90,200,16,16,"ATOM@ALIENTEK");
	LCD_ShowString(30,110,200,16,16,"2017/8/12");   
	LCD_ShowString(30,130,200,16,16,"KEY0:Test Sram");
	LCD_ShowString(30,150,200,16,16,"KEY1:TEST Data");
 	POINT_COLOR=BLUE;       //设置字体为蓝色

    for(ts=0;ts<250000;ts++)
	{
		testsram[ts]=ts;//预存测试数据	 
  	}
    while(1)
    {
		key=KEY_Scan(0);//不支持连按	
		if(key==KEY0_PRES)fsmc_sdram_test(30,170);//测试SRAM容量
		else if(key==KEY1_PRES)//打印预存测试数据
		{
			for(ts=0;ts<250000;ts++)
			{
				LCD_ShowxNum(30,190,testsram[ts],6,16,0);//显示测试数据	
				printf("testsram[%d]:%d\r\n",ts,testsram[ts]);
			}
		}else delay_ms(10);   
		i++;
		if(i==20)//DS0闪烁.
		{
			i=0;
			LED0_Toggle;
 		}
	} 
}

