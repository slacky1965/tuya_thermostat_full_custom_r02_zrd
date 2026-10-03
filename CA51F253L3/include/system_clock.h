#include "include/stdint.h"
// CKCON register bits
#define ILCKE		(1<<7)
#define IHCKE		(1<<6)
#define TFCKE		(1<<5)
#define XHCS		(1<<4)
#define XLCKE		(1<<3)
#define XLSTA		(1<<2)
#define XHCKE		(1<<1)
#define XHSTA		(1<<0)

// CKSEL register bits
#define RTCKS(N)		(N<<7)

#define CKSEL_IRCH		0	
#define CKSEL_IRCL		1
#define CKSEL_XOSCH		2
#define CKSEL_XOSCL		3
#define CKSEL_PLL			4
#define CKSEL_TFRC		5

// PLLCON register bits
#define PLLON(N)		(N<<7)
#define MULFT(N)		(N<<3)	  //N=0-8


#define PLSTA				(1<<0)

// RCCON register bits
#define MODE(N)			(N<<6)      //N=0~3
#define MSEX(N)			(N<<5)		//N=0~1
#define HMSK				(1<<4)
#define CKSS(N)			N	   			//N=0~11

void Sys_Clk_Set_IRCH(void);
void Sys_Clk_Set_IRCL(void);
void Sys_Clk_Set_XOSCL(void);
void Sys_Clk_Set_PLL(uint8_t Multiple);
void Sys_Clk_Set_XOSCH(void);

