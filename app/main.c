#include <pico/stdlib.h>
#include <stdio.h>
#include <tusb.h>

#include <hardware/rtc.h>

#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/rosc.h"
#include "hardware/structs/scb.h"

#include "debug.h"
#include "keyboard.h"
#include "puppet_i2c.h"
#include "reg.h"
#include "usb.h"

// https://github.com/micropython/micropython/blob/5114f2c1ea7c05fc7ab920299967595cfc5307de/ports/rp2/modmachine.c#L179
// https://github.com/raspberrypi/pico-extras/issues/41
#include "pico/sleep.h"

int main(void)
{
	// This order is important because it determines callback call order

#ifdef DEBUG
	debug_init();
#endif

	uart_deinit(uart0);
	uart_deinit(uart1);

	reg_init();

	keyboard_init();

	puppet_i2c_init();

	usb_init();

#ifndef NDEBUG
	printf("Starting main loop\r\n");
#endif

	while (true) {
		__wfe();
	}

	return 0;
}
