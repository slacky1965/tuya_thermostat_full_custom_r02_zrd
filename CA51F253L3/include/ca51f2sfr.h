#ifndef __CA51F2_H__
#define __CA51F2_H__

__sfr __at(0x80) P0;
__sfr __at(0x81) SP;
__sfr __at(0x82) DP0L;
__sfr __at(0x83) DP0H;
__sfr __at(0x84) DP1L;
__sfr __at(0x85) DP1H;
__sfr __at(0x86) PWCON;
__sfr __at(0x87) PCON;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0x8a) TL0;
__sfr __at(0x8b) TL1;
__sfr __at(0x8c) TH0;
__sfr __at(0x8d) TH1;
__sfr __at(0x8e) IT1CON;
__sfr __at(0x8f) IT0CON;

__sfr __at(0x90) P1;
__sfr __at(0x91) RCCON;
__sfr __at(0x92) VCKDL;
__sfr __at(0x93) VCKDH;
__sfr __at(0x94) RCTAGL;
__sfr __at(0x95) RCTAGH;
__sfr __at(0x96) RCMSLL;
__sfr __at(0x97) RCMSLH;
__sfr __at(0x98) S0CON;
__sfr __at(0x99) S0BUF;
__sfr __at(0x9a) S1CON;
__sfr __at(0x9b) S1BUF;
__sfr __at(0x9c) S1RELL;
__sfr __at(0x9d) S1RELH;
__sfr __at(0x9e) RCMSHL;
__sfr __at(0x9f) RCMSHH;

__sfr __at(0xa0) P2;
__sfr __at(0xa1) S2CON;
__sfr __at(0xa2) S2BUF;
__sfr __at(0xa3) S2RELL;
__sfr __at(0xa4) S2RELH;
__sfr __at(0xa5) SPCON;
__sfr __at(0xa6) SPDAT;
__sfr __at(0xa7) SPSTA;
__sfr __at(0xa8) IE;
__sfr __at(0xa9) P6;
__sfr __at(0xaa) WDCON;
__sfr __at(0xab) WDFLG;
__sfr __at(0xac) WDVTHL;
__sfr __at(0xad) WDVTHH;
__sfr __at(0xae) PLLCON;
__sfr __at(0xaf) HVTH;

__sfr __at(0xb0) P3;
__sfr __at(0xb1) I2CCON;
__sfr __at(0xb2) I2CADR;
__sfr __at(0xb3) I2CADM;
__sfr __at(0xb4) I2CCCR;		 
__sfr __at(0xb5) I2CDAT;
__sfr __at(0xb6) I2CSTA;
__sfr __at(0xb7) I2CFLG;
__sfr __at(0xb8) IP;
__sfr __at(0xb9) ADCON;
__sfr __at(0xba) ADCFGL;
__sfr __at(0xbb) ADCFGH;
__sfr __at(0xbc) ADCDL;
__sfr __at(0xbd) ADCDH;
__sfr __at(0xbe) CKMON;
__sfr __at(0xbf) CKMIF;

__sfr __at(0xc0) P4;
__sfr __at(0xc1) TKCON;
__sfr __at(0xc2) TKCFG;
__sfr __at(0xc3) TKMTS;
__sfr __at(0xc4) TKCHS;
__sfr __at(0xc5) ATKML;
__sfr __at(0xc6) ATKMH;
__sfr __at(0xc7) TKIF;
__sfr __at(0xc8) T2CON;
__sfr __at(0xc9) T2MOD;
__sfr __at(0xca) T2CL;
__sfr __at(0xcb) T2CH;
__sfr __at(0xcc) TL2;
__sfr __at(0xcd) TH2;
__sfr __at(0xce) TKMSL;
__sfr __at(0xcf) TKMSH;

__sfr __at(0xd0) PSW;
__sfr __at(0xd1) PWMDIVH;
__sfr __at(0xd2) PWMDUTL;
__sfr __at(0xd3) PWMDUTH;
__sfr __at(0xd4) PWMAIF;
__sfr __at(0xd5) PWMBIF;
__sfr __at(0xd6) PWMCIF;
__sfr __at(0xd7) PWMDIF;
__sfr __at(0xd8) P5;
__sfr __at(0xd9) P7;
__sfr __at(0xda) PWMEN;
__sfr __at(0xdb) PWMUPD;
__sfr __at(0xdc) PWMCMAX;
__sfr __at(0xdd) PWMCON;
__sfr __at(0xde) PWMCFG;
__sfr __at(0xdf) PWMDIVL;

__sfr __at(0xe0) ACC;
__sfr __at(0xe1) LXCON;
__sfr __at(0xe2) LXCFG;
__sfr __at(0xe3) LXDAT;
__sfr __at(0xe4) LXDIVL;
__sfr __at(0xe5) LXDIVH;
__sfr __at(0xe6) MDUCON;
__sfr __at(0xe7) MDUDAT;
__sfr __at(0xe8) EXIE;
__sfr __at(0xe9) RTCSS;
__sfr __at(0xea) RTAS;
__sfr __at(0xeb) RTAM;
__sfr __at(0xec) RTAH;
__sfr __at(0xed) RTMSS;
__sfr __at(0xee) RTCIF;
__sfr __at(0xef) LVDCON;

__sfr __at(0xf0) B;
__sfr __at(0xf1) RTCON;
__sfr __at(0xf2) RTCS;
__sfr __at(0xf3) RTCM;
__sfr __at(0xf4) RTCH;
__sfr __at(0xf5) RTCDL;
__sfr __at(0xf6) RTCDH;
__sfr __at(0xf7) INDEX;
__sfr __at(0xf8) EXIP;
__sfr __at(0xf9) EPIE;
__sfr __at(0xfa) EPIF;
__sfr __at(0xfb) EPCON;
__sfr __at(0xfc) IDLSTL;
__sfr __at(0xfd) IDLSTH;
__sfr __at(0xfe) STPSTL;
__sfr __at(0xff) STPSTH;


/*  BIT Register  */
/*	PSW            */
__sbit __at(0xD7) CY;
__sbit __at(0xD6) AC;
__sbit __at(0xD5) F0;
__sbit __at(0xD4) RS1;
__sbit __at(0xD3) RS0;
__sbit __at(0xD2) OV;
__sbit __at(0xD1) DPS;
__sbit __at(0xD0) P;
		  		
/*	TCON */
__sbit __at(0x8F) TF1;
__sbit __at(0x8E) TR1;
__sbit __at(0x8D) TF0;
__sbit __at(0x8C) TR0;
__sbit __at(0x8B) IE1;
__sbit __at(0x8A) IT1;
__sbit __at(0x89) IE0;
__sbit __at(0x88) IT0;

/*	S0CON   */
__sbit __at(0x9F) SM0;
__sbit __at(0x9E) SM1;
__sbit __at(0x9D) SM2;
__sbit __at(0x9C) REN;
__sbit __at(0x9B) TB8;
__sbit __at(0x9A) RB8;
__sbit __at(0x99) TI0;
__sbit __at(0x98) RI0;

/*	IE */
__sbit __at(0xAF) EA;
__sbit __at(0xAE) ES1;
__sbit __at(0xAD) ET2;
__sbit __at(0xAC) ES0;
__sbit __at(0xAB) ET1;
__sbit __at(0xAA) EX1;
__sbit __at(0xA9) ET0;
__sbit __at(0xA8) EX0;

/*	IP */
__sbit __at(0xBE) PS1;
__sbit __at(0xBD) PT2;
__sbit __at(0xBC) PS0;
__sbit __at(0xBB) PT1;
__sbit __at(0xBA) PX1;
__sbit __at(0xB9) PT0;
__sbit __at(0xB8) PX0;

/*	T2CON */
__sbit __at(0xCF) TF2;
__sbit __at(0xCE) TR2;
__sbit __at(0xCD) T2R1;
__sbit __at(0xCC) T2R0;
__sbit __at(0xCB) T2IE;
__sbit __at(0xCA) UCKS;
__sbit __at(0xC9) T2P1;
__sbit __at(0xC8) T2P0;

/*	EXIE */
__sbit __at(0xEF) INT9EN;
__sbit __at(0xEE) INT8EN;
__sbit __at(0xED) INT7EN;
__sbit __at(0xEC) INT6EN;
__sbit __at(0xEB) INT5EN;
__sbit __at(0xEA) INT4EN;
__sbit __at(0xE9) INT3EN;
__sbit __at(0xE8) INT2EN;

/*	P0 */
__sbit __at(0x80) P00;
__sbit __at(0x81) P01;
__sbit __at(0x82) P02;
__sbit __at(0x83) P03;
__sbit __at(0x84) P04;
__sbit __at(0x85) P05;
__sbit __at(0x86) P06;
__sbit __at(0x87) P07;
/*	P1 */
__sbit __at(0x90) P10;
__sbit __at(0x91) P11;
__sbit __at(0x92) P12;
__sbit __at(0x93) P13;
__sbit __at(0x94) P14;
__sbit __at(0x95) P15;
__sbit __at(0x96) P16;
__sbit __at(0x97) P17;
/*	P2 */
__sbit __at(0xA0) P20;
__sbit __at(0xA1) P21;
__sbit __at(0xA2) P22;
__sbit __at(0xA3) P23;
__sbit __at(0xA4) P24;
__sbit __at(0xA5) P25;
__sbit __at(0xA6) P26;
__sbit __at(0xA7) P27;

/*	P3 */
__sbit __at(0xB0) P30;
__sbit __at(0xB1) P31;
__sbit __at(0xB2) P32;
__sbit __at(0xB3) P33;
__sbit __at(0xB4) P34;
__sbit __at(0xB5) P35;
__sbit __at(0xB6) P36;
__sbit __at(0xB7) P37;		

/*	P4 */
__sbit __at(0xC0) P40;
__sbit __at(0xC1) P41;
__sbit __at(0xC2) P42;
__sbit __at(0xC3) P43;
__sbit __at(0xC4) P44;
__sbit __at(0xC5) P45;
__sbit __at(0xC6) P46;
__sbit __at(0xC7) P47;	

///*	P5	*/
__sbit __at(0xD8) P50;
__sbit __at(0xD9) P51;
__sbit __at(0xDA) P52;
__sbit __at(0xDB) P53;
__sbit __at(0xDC) P54;
__sbit __at(0xDD) P55;
__sbit __at(0xDE) P56;
__sbit __at(0xDF) P57;
//
///*	P6	*/
#define P60(n)	{if(n) P6|=0x01;else P6&=~0x01;}
#define P61(n)	{if(n) P6|=0x02;else P6&=~0x02;}
#define P62(n)	{if(n) P6|=0x04;else P6&=~0x04;}
#define P63(n)	{if(n) P6|=0x08;else P6&=~0x08;}
#define P64(n)	{if(n) P6|=0x10;else P6&=~0x10;}
#define P65(n)	{if(n) P6|=0x20;else P6&=~0x20;}
#define P66(n)	{if(n) P6|=0x40;else P6&=~0x40;}
#define P67(n)	{if(n) P6|=0x80;else P6&=~0x80;}
//
///*	P7	*/
#define P70(n)	{if(n) P7|=0x01;else P7&=~0x01;}
#define P71(n)	{if(n) P7|=0x02;else P7&=~0x02;}
#define P72(n)	{if(n) P7|=0x04;else P7&=~0x04;}
#define P73(n)	{if(n) P7|=0x08;else P7&=~0x08;}
#define P74(n)	{if(n) P7|=0x10;else P7&=~0x10;}
#define P75(n)	{if(n) P7|=0x20;else P7&=~0x20;}

#endif  
