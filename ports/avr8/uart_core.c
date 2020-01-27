#include "py/mpconfig.h" // gives F_CPU
#define BAUD 9600

#include <avr/io.h>
#include <util/delay.h>
#include <util/setbaud.h>


void uart_init(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

#if USE_2X // defined by setbaud.h
    UCSR0A |= _BV(U2X0);
#else
    UCSR0A &= ~(_BV(U2X0));
#endif

    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
}

static unsigned char uart_read(void) {
    while (!(UCSR0A & (1 << RXC0)));
    return UDR0;
}

static void uart_write(unsigned char data) {
    if (data == '\n') {
        uart_write('\r');
    }

    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;
}

int mp_hal_stdin_rx_chr(void) {
    return (int)uart_read();
}

// Send string of given length dummy
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    for (mp_uint_t i = 0; i < len; ++i) {
        uart_write(str[i]);
    }
}
