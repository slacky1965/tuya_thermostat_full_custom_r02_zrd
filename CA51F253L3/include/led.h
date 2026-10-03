#ifndef LED_H
#define LED_H
#include "include/stdint.h"
/********************************************************************************/
/* LED display + backlight module.                                              */
/*                                                                              */
/*  REG-COM matrix (on-board LED driver):                                       */
/*    COM0..COM7 = P0.0..P0.7 (pins 61..54), high sink current (PxxC|=0x80)     */
/*    SEG12..SEG22 = P6.2..P6.5 + P1.0..P1.6 (pins 34..44)                      */
/*    brightness: LDRV[2:0] in LXCFG (8 levels), display buffer via INDEX/LXDAT */
/*                                                                              */
/*  PWM LEDs (backlight):                                                       */
/*    P5.0/PWM0 = backlight 4 touch keys, P5.1/PWM1 = red strip,                */
/*    P5.2/PWM2 = white backlight of power key, P5.3/PWM3 = green backlight     */
/*    brightness: PWM duty (Pwm_Duty()), period via PWMDIV.                     */
/********************************************************************************/
// LXCON register bits
#define LEN(N)		(N<<5) 	//N=0~6
#define LMOD(N)		(N<<4)	//N=0~1


// LXCFG register bits
#define DMOD(N)		(N<<6)
#define BIAS(N)		(N<<4)

#define LDRV(N)		(N) //N=0-7

#define COMHV(N)	(N<<5)	//N=0~1
#define SEGHV(N)	(N<<4)	//N=0~1
#define BLNK(N)		(N<<3)	//N=0~1

enum 
{
  LEN_DISABLE	= 0,
  LEN_IRCL  	= 1,
  LEN_IRCH  	= 2,
  LEN_XOSCL  	= 3,
  LEN_PLL  		= 5,
  LEN_TF  		= 6,
};

enum 
{
  LMOD_lcd	= 0,
  LMOD_led  = 1,
};

enum 
{
  DMOD_5ua		= 0,
  DMOD_40ua  	= 1,
  DMOD_80ua 	= 2,
  DMOD_130ua 	= 3,
};

enum 
{
  BIAS_1_4		= 0,
  BIAS_1_2  	= 1,
  BIAS_1_3 		= 2,
};

enum 
{
  COM_L  		= 0,
  COM_H		= 1,
};

enum 
{
  SEG_L  		= 0,
  SEG_H		= 1,
};

enum 
{
  LDRV_0		= 0,
  LDRV_1		= 1,
  LDRV_2		= 2,
  LDRV_3		= 3,
  LDRV_4		= 4,
  LDRV_5		= 5,
  LDRV_6		= 6,
  LDRV_7		= 7,
};
/*********************************************************************************/
/* PWM channels (backlight LEDs)                                                 */
#define PWM_CH0		0
#define PWM_CH1		1
#define PWM_CH2		2
#define PWM_CH3		3
#define PWM_CH4		4
#define PWM_CH5		5
#define PWM_CH6		6
#define PWM_CH7		7

/* Semantic aliases: which backlight each PWM channel drives                     */
#define BL_KEYS          PWM_CH0   /* backlight of the 4 touch keys              */
#define BL_RED           PWM_CH1   /* red strip                                  */
#define BL_WHITE         PWM_CH2   /* white backlight of the power (leaf) key    */
#define BL_GREEN         PWM_CH3   /* green backlight                            */

/*********************************************************************************/
/* Unified brightness: level 0 = off, 1-8 = brightness (LDRV 0-7 + PWM duty)     */
#define LED_BRIGHTNESS_OFF  0
#define LED_BRIGHTNESS_MIN  1
#define LED_BRIGHTNESS_MAX  8

/* On-screen symbols. One symbol = one LED = one cell (SEG x COM) of the        */
/* REG-COM matrix. Map taken from the confirmed table in                        */
/* doc_local/architecture_TZE204_edl8pz1k.md (LED map).                         */
/* Indexes below ARE the row numbers of sym_map[] in led.c, keep in sync.       */

enum
{
	SYM_SUNNY   = 0,  /* sunny pictogram    SEG12/COM0  idx0 b0   */
	SYM_CLOUD   = 1,  /* cloud pictogram    SEG12/COM1  idx0 b1   */
	SYM_RAIN    = 2,  /* rain pictogram     SEG12/COM2  idx0 b2   */
	SYM_SNOW    = 3,  /* snow pictogram     SEG12/COM3  idx0 b3   */
	SYM_HEAT   = 4,   /* flame: heating active SEG13/COM1 idx1 b1 */
	SYM_DEGC   = 5,   /* degree C unit      SEG14/COM7  idx2 b7   */
	SYM_DP5    = 6,   /* half-degree dot    SEG15/COM7  idx3 b7   */
	SYM_MON    = 7,   /* weekday Mon        SEG20/COM0  idx8 b0   */
	SYM_TUE    = 8,   /* weekday Tue        SEG20/COM1  idx8 b1   */
	SYM_WED    = 9,   /* weekday Wed        SEG20/COM2  idx8 b2   */
	SYM_THU    = 10,  /* weekday Thu        SEG20/COM3  idx8 b3   */
	SYM_FRI    = 11,  /* weekday Fri        SEG20/COM4  idx8 b4   */
	SYM_SAT    = 12,  /* weekday Sat        SEG20/COM5  idx8 b5   */
	SYM_SUN     = 13, /* weekday Sun        SEG20/COM6  idx8 b6   */
	SYM_NET    = 14,  /* network            SEG20/COM7  idx8 b7   */
	SYM_SP1    = 15,  /* schedule period 1  SEG21/COM0  idx9 b0   */
	SYM_SP2    = 16,  /* schedule period 2  SEG21/COM1  idx9 b1   */
	SYM_SP3    = 17,  /* schedule period 3  SEG21/COM2  idx9 b2   */
	SYM_SP4    = 18,  /* schedule period 4  SEG21/COM3  idx9 b3   */
	SYM_SP5    = 19,  /* schedule period 5  SEG21/COM4  idx9 b4   */
	SYM_SP6    = 20,  /* schedule period 6  SEG21/COM5  idx9 b5   */
	SYM_PM     = 21,  /* PM                 SEG21/COM6  idx9 b6   */
	SYM_AM     = 22,  /* AM                 SEG21/COM7  idx9 b7   */
	SYM_MOON   = 23,  /* crescent moon      SEG22/COM0  idx10 b0  */
	SYM_CLOCK  = 24,  /* clock              SEG22/COM1  idx10 b1  */
	SYM_HAND   = 25,  /* hand               SEG22/COM2  idx10 b2  */
	SYM_HEATER = 26,  /* heater             SEG22/COM3  idx10 b3  */
	SYM_SET    = 27,  /* Set                SEG22/COM4  idx10 b4  */
	SYM_HOME   = 28,  /* house+thermometer  SEG22/COM5  idx10 b5  */
	SYM_LOCK   = 29,  /* padlock            SEG22/COM6  idx10 b6  */
	SYM_ECO    = 30,  /* leaf in circle     SEG22/COM7  idx10 b7  */
	SYM_HUMID  = 31,  /* %RH (humidity window) SEG19/COM7 idx7 b7 */
	SYM_COUNT  = 32
};

void LedDrv_Init(void);                        /* REG-COM LEDs: pins + peripheral (all off until LXDAT) */
void Pwm_Backlight_Init(void);                 /* PWM LEDs: pins + PWM channels (factory style)         */
void Pwm_Duty(uint8_t ch, uint8_t pct);        /* PWM LEDs: duty 0..100% on channel ch                  */
void Led_SetBrightness(uint8_t level);         /* REG-COM display: 0=off, 1-8=LDRV brightness           */
void Led_Backlight(uint8_t ch, uint8_t level); /* one PWM LED: 0=off, 1-8=brightness                    */
void Led_BootTest(void);                       /* 3 s all on, then 2 s the version in the big digits    */
/* Shadow-buffer symbol API: draw into led_buf[], Led_Flush() pushes to display */
void Led_SymInit(void);                    /* clear shadow buffer + flush (display off)        */
void Led_SymSet(uint8_t id, uint8_t on);   /* turn symbol id on/off (no flush)                 */
void Led_Flush(void);                      /* write the whole shadow buffer to the display     */
void Led_CellScan(void);                   /* bench: light one cell at a time (map the matrix) */
/* Digit/temperature rendering (7-seg). Digits on the big positions:            */
/* INDEX3 = tens (SEG15 "second digit"), INDEX2 = units (SEG14 "third digit").  */
void Led_Digit(uint8_t idx, uint8_t digit);                               /* 7-seg digit 0..9 into buffer (keeps bit7 icons)           */
void Led_Char(uint8_t idx, uint8_t seg7);                                 /* raw 7-seg pattern bits0-6 (bit7 kept)                     */
void Led_Letters(uint8_t tens_seg7, uint8_t units_seg7);                  /* two letters in the big digits                             */
void Led_SensorLetters(uint8_t src);                                      /* IN/OU/AL word, shared table in led.c                      */
void Led_HalfDot(uint8_t on);                                             /* big temp '.5' dot (buffer word 3, bit7)                   */
void Led_Weekday(uint8_t week);                                           /* light weekday 1..7 (Mon=1) in buffer, bit7 (SYM_NET) kept */
void Led_Clock(uint8_t hour, uint8_t min, uint8_t colon_on, uint8_t h24); /* h24=1 -> 24h, h24=0 -> 12h AM/PM                          */
void Led_Clock_Blank(uint8_t field);                                      /* blank hours(0)/minutes(1) field, keeps ':' and AM/PM      */
void Led_Temp(int value_c100);                                            /* draw temp: two digits + hundreds '1' + .5 + degree C      */
void Led_MenuValue(int16_t value_c100);                                   /* menu value without unit, hundreds, or flush               */
void Led_Temp_Blank(void);                                                /* blank the temperature digits (keeps '.5' / degree C)      */
void Led_TempErr(void);                                                   /* draw "Er" in the big digits (sensor error)                */

#endif
