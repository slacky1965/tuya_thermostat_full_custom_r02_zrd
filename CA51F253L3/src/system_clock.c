/*********************************************************************************************************************/
#include "include/stdint.h"
#include "include/config.h"		
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"

#include "include/delay.h"
#include <intrins.h>

#define SYSCLK_XOSCL_MAXWAIT 98
/*********************************************************************************************************************/


/***********************************************************************************
Function:		Sys_Clk_Set_IRCH
Description:	switch system clock to IRCH
Input:			none
Return:			none
***********************************************************************************/
void Sys_Clk_Set_IRCH(void) {
	CKCON |= IHCKE;											// enable IRCH clock
	CKSEL = (CKSEL&0xF8) | CKSEL_IRCH;                      // switch system clock to IRCH
}
/***********************************************************************************/

/* The helpers below have no callers in this firmware (RTC_Init() owns the XOSCL
   wait and the IRCL fallback, main() only uses IRCH) but the linker keeps every
   compiled function, so they are gated by config.h SYSCLK_EXTRA_FUNCS.         */
#if SYSCLK_EXTRA_FUNCS


/***********************************************************************************
Function:		Sys_Clk_Set_IRCL
Description:	switch system clock to IRCL
Input:			none
Return:			none
***********************************************************************************/
void Sys_Clk_Set_IRCL(void) {
	CKCON |= ILCKE;											// enable IRCL clock
	Delay_ms(1);                                            // wait 1ms for IRCL to stabilize
	CKSEL = (CKSEL&0xF8) | CKSEL_IRCL;                      // switch system clock to IRCL
}
/***********************************************************************************/



/***********************************************************************************
Function:		Sys_Clk_Set_XOSCL
Description:	switch system clock to XOSCL
Input:			none
Return:			none
***********************************************************************************/
void Sys_Clk_Set_XOSCL(void) {
	uint8_t i;

	P71F = P71_XOSCL_OUT_SETTING;
	P72F = P72_XOSCL_IN_SETTING;
	CKCON |= XLCKE;
	i = SYSCLK_XOSCL_MAXWAIT;
	do {
		WDFLG = 0xA5;
		if(CKCON & XLSTA) {
			CKSEL = (CKSEL & 0xF8) | CKSEL_XOSCL;
			return;
		}
		Delay_ms(10);
	} while(--i);
	Sys_Clk_Set_IRCL();
	CKCON &= (uint8_t)~XLCKE;
}
/***********************************************************************************/



/***********************************************************************************
Function:		Sys_Clk_Set_PLL
Description:	switch system clock to PLL
Input:			Multiple   multiplier
Return:			none
***********************************************************************************/
void Sys_Clk_Set_PLL(uint8_t Multiple)	 {
	if(Multiple < 2 || Multiple > 8) return;		// valid multiplier range 2~8, out of range returns

	PLLCON = PLLON(1) | MULFT(Multiple-2);                          // configure multiplier and enable PLL
	while(!(PLLCON & PLSTA));										// wait for PLL to stabilize
	CKSEL = (CKSEL&0xF8) | CKSEL_PLL;                               // switch system clock to PLL
}
/***********************************************************************************/



/***********************************************************************************
Function:		Sys_Clk_Set_TFRC
Description:	switch system clock to TFRC
Input:			none
Return:			none
***********************************************************************************/
void Sys_Clk_Set_TFRC(void) {
	CKCON |= TFCKE;													// enable TFRC clock
	CKSEL = (CKSEL&0xF8) | CKSEL_TFRC;                              // switch system clock to TFRC
}
/***********************************************************************************/



/***********************************************************************************
Function:		Sys_Clk_Set_XOSCH
Description:	switch system clock to XOSCH
Input:			none
Return:			none
***********************************************************************************/
void Sys_Clk_Set_XOSCH(void) {
	CKCON ^= XHCS;	
	CKCON ^= XHCS;
	CKSEL = (CKSEL&0xC7) | 0x38;				// set drive strength
	P74F = 3;                                   // P7.4 as XOSCH output
	P73F = 3;                                   // P7.3 as XOSCH input
	CKCON |= XHCKE;                             // enable XOSCH clock
	while(!(CKCON & XHSTA));                    // wait for XOSCH to stabilize
	CKSEL = (CKSEL&0xF8) | CKSEL_XOSCH;			// switch system clock to XOSCH
}
/***********************************************************************************/

#endif /* SYSCLK_EXTRA_FUNCS */