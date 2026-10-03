
/* Timer0 ISR (vector 1) lives in main.c - 1 s time base */
void isr_vect_t1(void) __interrupt (3) { }
void isr_vect_uart0(void) __interrupt (4) { }
/* UART1 ISR (vector 6) lives in uart.c - the ZT3L link */
void isr_vect_uart2_int3(void) __interrupt (8) { }