#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/debug.h"
#include "include/mdu.h"
#include "include/intrins.h"
/*******************************************************************************
	Lightweight polled UART0 debug output @115200 (P3.0 TX / P3.1 RX,
	Timer2 as baudrate generator). No interrupts, no ring buffer - a few
	tens of bytes against the vendor's interrupt uart_printf + 256 B buffer.
	Print gating lives in debug.h (BUTTONS_EN/LED_EN/...): call sites use
	DBG_BUTTONS()/DBG_LED() wrappers which expand to nothing when 0.
********************************************************************************/

#if UART_DEBUG

/* FOSC and the debug baudrate are compile-time constants, so the reload folds
   to a single number - no runtime 32-bit divide / MDU call (same approach as
   Uart1_Initial() in uart.c). On IRCH (3686400) at 115200 that is
   0x10000 - 3686400/(115200*32) = 0xFFFF = UART0_RELOAD_FALLBACK, i.e. the
   value the old divide produced.                                             */
#define DEBUG_BAUDRATE  115200UL
#define DEBUG_RELOAD    ((uint16_t)(0x10000UL - (FOSC / (DEBUG_BAUDRATE * 32UL))))

void debug_init(void) {
	uint16_t value_temp;

	P31F = P31_UART0_RX_SETTING;
	P30F = P30_UART0_TX_SETTING;

	value_temp = DEBUG_RELOAD;
	T2CON = 0x24;
	T2CH  = (uint8_t)(value_temp>>8);
	T2CL  = (uint8_t)(value_temp);
	TH2   = (uint8_t)(value_temp>>8);
	TL2   = (uint8_t)(value_temp);
	TR2   = 1;

	S0CON = 0x50;
}

void debug_putc(uint8_t c) {
	S0BUF = c;
	while(!TI0);
	TI0 = 0;
}

/* -------- fixed-format primitives: no varargs, no format strings ----------- */
/* Every DEBUG(flag, ...) call site prints through these. That is what keeps a
   single debug flag inside the 802 B ROM debug budget: Digits are produced by
   subtracting power-of-ten places instead of / and %, so no __divsint or
   __modsint gets linked in. The varargs formatter (ca_printf) was deleted
   2026-09-28 - no call site used it and it cost ~1500 B.                     */

/* The linker pulls the WHOLE debug.rel object in once any symbol of it is
   referenced, so compiling the text/number primitives for a build that only
   needs debug_putc (LOCK_TRACE_EN trace) would cost ~270 B of ROM for nothing.
   Compile them only when a verbose module that actually calls them is ON.    */
#if UART_DEBUG && (BUTTONS_EN || LED_EN || RAW_EN || TEMP_EN || RTC_EN || \
                   LINK_EN || FLASH_EN || SETTINGS_EN || TIMER_EN || \
                   LED_SCAN_EN || FLASHER_EN)
#define DEBUG_PRIMITIVES 1
#endif

#ifdef DEBUG_PRIMITIVES
/* code-qualified pointer: string literals live in code space, so this stays a
   16-bit MOVC read and does not pull the generic __gptrget helper into ROM. */
#if defined(__clang__)
/* the clang-only cast macro in debug.h would rewrite this definition (it
   looks like a call of the macro); drop it here and restore after the body   */
#undef debug_puts
#endif
void debug_puts(const char code *s) {
	while(*s) debug_putc((uint8_t)*s++);
}
#if defined(__clang__)
#define debug_puts(s) debug_puts((const char code *)(s))
#endif

/* Five decimal places (max 99999) cover every value in the firmware:
   temperatures come from an NTC table capped at 6000 c100 (+/- calib),
   setpoints and limits are <= 4500, the WS sensor reports up to 12500 c100
   (100.00..125.00 degC) and the RTC day counter runs past 20000. The fifth
   place (10000) was removed 2026-09-28 under ROM pressure in debug builds
   and restored 2026-10-02 (audit M12): values >= 10000 used to print as
   '0'+10 garbage. Only debug builds pay for it - UART_DEBUG=OFF links
   nothing from this file. */
static const uint16_t code debug_place[5] = { 10000, 1000, 100, 10, 1 };

/* decimal, minimal width (no leading zero): replaces %d / %u.
   The zero-padded form is a separate, RTC-only helper (debug_dec2).          */
void debug_dec(uint16_t v) {
	uint8_t i, d, started = 0;

	for(i = 0; i < 5; i++) {
		d = 0;
		while(v >= debug_place[i]) {
			v = (uint16_t)(v - debug_place[i]);
			d++;
		}
		if(d || started || i == 4) {
			debug_putc((uint8_t)('0' + d));
			started = 1;
		}
	}
}

#if UART_DEBUG && RTC_EN
/* zero-padded two digits, replaces %02u (the clock line hh:mm:ss)           */
void debug_dec2(uint8_t v) {
	uint8_t d = 0;

	while(v >= 10) {
		v = (uint8_t)(v - 10);
		d++;
	}
	debug_putc((uint8_t)('0' + d));
	debug_putc((uint8_t)('0' + v));
}
#endif /* RTC_EN */

/* signed decimal: replaces %d (and %u for values below 32768)               */
void debug_i16(int16_t v) {
	if(v < 0) {
		debug_putc('-');
		debug_dec((uint16_t)(0u - (uint16_t)v));
	}
	else {
		debug_dec((uint16_t)v);
	}
}

/* "<tag><decimal>\r\n" in one call: the common dump line ("sp 2100\r\n").
   Three calls at the site cost ~27 B of code, this costs ~9 B plus a ~50 B
   helper once - a net win from the third site onwards.                       */
#if defined(__clang__)
#undef debug_kv
#endif
void debug_kv(const char code *tag, int16_t v) {
	debug_puts(tag);
	debug_i16(v);
	debug_puts("\r\n");
}
#if defined(__clang__)
#define debug_kv(tag, v) debug_kv((const char code *)(tag), (v))
#endif
#endif /* DEBUG_PRIMITIVES */

#if UART_DEBUG && (RAW_EN || LINK_EN || LOCK_TRACE_EN)
/* fixed-width hex, replaces %02X/%04X (raw touch readings and the link dump).
   Every call site passes a constant width (2 or 4), so there is no nibble table
   here - that machinery alone cost 148 B. The `digits == 2` pre-shift IS the
   right-alignment of %02X: the loop always prints the TOP nibbles, so a byte
   (0x0066) must be moved into the high position first or it comes out "00".
   Do NOT drop it as dead code - bench 2026-09-28 (LINK_EN) showed exactly that
   regression: every dumped byte printed as 00 while the frame lengths were
   right.                                                                 */
void debug_hex(uint16_t v, uint8_t digits) {
	uint8_t i, n;

	if(digits == 2) v = (uint16_t)(v << 8);   /* %02X: show the low byte     */

	for(i = 0; i < digits; i++) {
		n = (uint8_t)(v >> 12);
		v = (uint16_t)(v << 4);
		debug_putc((uint8_t)(n < 10 ? (uint8_t)('0' + n)
		                            : (uint8_t)('A' + n - 10)));
	}
}
#endif /* RAW_EN || LINK_EN */

#if UART_DEBUG && TEMP_EN
/* -------- temperature log: four signed c100 samples, fixed format ---------- */

void debug_temp(int16_t in_live, int16_t in_acc,
                int16_t ext_live, int16_t ext_acc) {
	debug_puts("temp: i=");
	debug_i16(in_live);
	debug_putc('/');
	debug_i16(in_acc);
	debug_puts(" o=");
	debug_i16(ext_live);
	debug_putc('/');
	debug_i16(ext_acc);
	debug_puts("\r\n");
}

#endif /* UART_DEBUG && TEMP_EN */

#endif
