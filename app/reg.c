#include "reg.h"

#include "app_config.h"
#include "backlight.h"
#include "fifo.h"
#include "gpioexp.h"
#include "puppet_i2c.h"
#include "keyboard.h"
#include "touchpad.h"
#include "pi.h"
#include "hardware/adc.h"
#include "rtc.h"

#include <pico/stdlib.h>
#include <RP2040.h> // TODO: When there's more than one RP chip, change this to be more generic
#include <stdio.h>

// We don't enable this by default cause it spams quite a lot
//#define DEBUG_REGS

static struct
{
	uint8_t regs[REG_ID_LAST];
} self;

static void touch_cb(int8_t x, int8_t y)
{
	const int16_t dx = (int8_t)self.regs[REG_ID_TOX] + x;
	const int16_t dy = (int8_t)self.regs[REG_ID_TOY] + y;

	// bind to -128 to 127
	self.regs[REG_ID_TOX] = MAX(INT8_MIN, MIN(dx, INT8_MAX));
	self.regs[REG_ID_TOY] = MAX(INT8_MIN, MIN(dy, INT8_MAX));
}
static struct touch_callback touch_callback = { .func = touch_cb };

void reg_process_packet(uint8_t in_reg, uint8_t in_data, uint8_t *out_buffer, uint8_t *out_len)
{
	const bool is_write = (in_reg & PACKET_WRITE_MASK);
	const uint8_t reg = (in_reg & ~PACKET_WRITE_MASK);
	uint16_t adc_value;

//	printf("read complete, is_write: %d, reg: 0x%02X\r\n", is_write, reg);

	*out_len = 0;

	switch (reg) {

	// common R/W registers
	case REG_ID_CFG:
	case REG_ID_INT:
	case REG_ID_DEB:
	case REG_ID_FRQ:
	case REG_ID_BKL:
	case REG_ID_BK2:
	case REG_ID_GIC:
	case REG_ID_GIN:
	case REG_ID_HLD:
	case REG_ID_ADR:
	case REG_ID_IND:
	case REG_ID_CF2:
	case REG_ID_DRIVER_STATE:
	case REG_ID_SHUTDOWN_GRACE:
	{
		if (is_write) {
			reg_set_value(reg, in_data);

			switch (reg) {
			case REG_ID_BKL:
			case REG_ID_BK2:
				backlight_sync();
				break;

			case REG_ID_ADR:
				puppet_i2c_sync_address();
				break;

			default:
				break;
			}
		} else {
			out_buffer[0] = reg_get_value(reg);
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	// special R/W registers
	case REG_ID_DIR: // gpio direction
	case REG_ID_PUE: // gpio input pull enable
	case REG_ID_PUD: // gpio input pull direction
	{
		if (is_write) {
			switch (reg) {
			case REG_ID_DIR:
				gpioexp_update_dir(in_data);
				break;
			case REG_ID_PUE:
				gpioexp_update_pue_pud(in_data, reg_get_value(REG_ID_PUD));
				break;
			case REG_ID_PUD:
				gpioexp_update_pue_pud(reg_get_value(REG_ID_PUE), in_data);
				break;
			}
		} else {
			out_buffer[0] = reg_get_value(reg);
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	case REG_ID_GIO: // gpio value
	{
		if (is_write) {
			gpioexp_set_value(in_data);
		} else {
			out_buffer[0] = gpioexp_get_value();
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	// RGB LED registers
	case REG_ID_LED_R:
	case REG_ID_LED_G:
	case REG_ID_LED_B:
	{
		if (is_write) {
			reg_set_value(reg, in_data);
			led_sync();
		} else {
			out_buffer[0] = reg_get_value(reg);
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	case REG_ID_LED: // gpio value
	{
		if (is_write) {
			reg_set_value(reg, in_data);
			led_sync();
		} else {
			out_buffer[0] = reg_get_value(reg);
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	// Rewake on timer
	case REG_ID_REWAKE_MINS:
	{
		// Only run this if driver was loaded
		// Otherwise, OS won't get the power key event
		if (reg_get_value(REG_ID_DRIVER_STATE) == 0) {
			break;
		}

		// Get rewake and grace times in milliseconds
		uint32_t rewake_ms = in_data * 60 * 1000;
		uint32_t shutdown_grace_ms = MAX(
			reg_get_value(REG_ID_SHUTDOWN_GRACE) * 1000,
			MINIMUM_SHUTDOWN_GRACE_MS);

		// Check input time against shutdown grace time
		// Plus some slop to allow for power cycling
		if (rewake_ms < (shutdown_grace_ms + 5000)) {
			break;
		}

		// Send shutdown signal to OS
		keyboard_inject_power_key();

		// Power off with grace time to give Pi time to shut down
		pi_schedule_power_off(shutdown_grace_ms);

		// Schedule power on
		pi_schedule_power_on(rewake_ms);

		break;
	}

	// Real time clock
	case REG_ID_RTC_SEC:
	case REG_ID_RTC_MIN:
	case REG_ID_RTC_HOUR:
	case REG_ID_RTC_MDAY:
	case REG_ID_RTC_MON:
	case REG_ID_RTC_YEAR:
	{
		if (is_write) {
			reg_set_value(reg, in_data);
		} else {
			out_buffer[0] = rtc_get(reg);
			*out_len = sizeof(uint8_t);
		}
		break;
	}

	case REG_ID_RTC_COMMIT:
	{
		rtc_set(reg_get_value(REG_ID_RTC_YEAR), reg_get_value(REG_ID_RTC_MON),
			reg_get_value(REG_ID_RTC_MDAY), reg_get_value(REG_ID_RTC_HOUR),
			reg_get_value(REG_ID_RTC_MIN), reg_get_value(REG_ID_RTC_SEC));
		break;
	}

	// read-only registers
	case REG_ID_TOX:
	case REG_ID_TOY:
		out_buffer[0] = reg_get_value(reg);
		*out_len = sizeof(uint8_t);

		reg_set_value(reg, 0);
		break;

	case REG_ID_VER:
		out_buffer[0] = VER_VAL;
		*out_len = sizeof(uint8_t);
		break;

	case REG_ID_ADC:
		adc_value = adc_read();
		out_buffer[0] = (uint8_t)(adc_value & 0x00FF);
		out_buffer[1] = (uint8_t)((adc_value & 0xFF00) >> 8);
		*out_len = sizeof(uint8_t) * 2;
		break;

	case REG_ID_KEY:
		out_buffer[0] = fifo_count();
		*out_len = sizeof(uint8_t);
		break;

	case REG_ID_FIF:
	{
		struct fifo_item item = fifo_dequeue();

		out_buffer[0] = ((uint8_t*)&item)[0];
		out_buffer[1] = ((uint8_t*)&item)[1];
		*out_len = sizeof(uint8_t) * 2;
		break;
	}

	case REG_ID_RST:
		NVIC_SystemReset();
		break;

	case REG_ID_STARTUP_REASON:
		out_buffer[0] = reg_get_value(reg);
		*out_len = sizeof(uint8_t);
		break;
	}
}

uint8_t reg_get_value(enum reg_id reg)
{
	return self.regs[reg];
}

void reg_set_value(enum reg_id reg, uint8_t value)
{
#ifdef DEBUG_REGS
	printf("%s: reg: 0x%02X, val: 0x%02X (%d)\r\n", __func__, reg, value, value);
#endif

	self.regs[reg] = value;
}

bool reg_is_bit_set(enum reg_id reg, uint8_t bit)
{
	return self.regs[reg] & bit;
}

void reg_set_bit(enum reg_id reg, uint8_t bit)
{
#ifdef DEBUG_REGS
	printf("%s: reg: 0x%02X, bit: %d\r\n", __func__, reg, bit);
#endif

	self.regs[reg] |= bit;
}

void reg_clear_bit(enum reg_id reg, uint8_t bit)
{
#ifdef DEBUG_REGS
	printf("%s: reg: 0x%02X, bit: %d\r\n", __func__, reg, bit);
#endif

	self.regs[reg] &= ~bit;
}

void reg_init(void)
{
	reg_set_value(REG_ID_CFG, CFG_OVERFLOW_INT | CFG_KEY_INT | CFG_USE_MODS);
	reg_set_value(REG_ID_BKL, 255);
	reg_set_value(REG_ID_DEB, 10);
	reg_set_value(REG_ID_FRQ, 10);	// ms
	reg_set_value(REG_ID_BK2, 255);
	reg_set_value(REG_ID_PUD, 0xFF);
	reg_set_value(REG_ID_HLD, 100);	// 10ms units
	reg_set_value(REG_ID_ADR, 0x1F);
	reg_set_value(REG_ID_IND, 1);	// ms
	reg_set_value(REG_ID_CF2, CF2_TOUCH_INT | CF2_USB_KEYB_ON | CF2_USB_MOUSE_ON);
	reg_set_value(REG_ID_DRIVER_STATE, 0); // Driver not yet loaded

	reg_set_value(REG_ID_SHUTDOWN_GRACE, 45);

	touchpad_add_touch_callback(&touch_callback);
}
