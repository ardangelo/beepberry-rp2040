#include "usb.h"

#include "keyboard.h"
#include "reg.h"

#include <hardware/irq.h>
#include <pico/mutex.h>
#include <tusb.h>

#define USB_LOW_PRIORITY_IRQ	31
#define USB_TASK_INTERVAL_US	1000

static struct
{
	mutex_t mutex;

	uint8_t write_buffer[2];
	uint8_t write_len;
} self;

static void low_priority_worker_irq(void)
{
	if (mutex_try_enter(&self.mutex, NULL)) {
		tud_task();

		mutex_exit(&self.mutex);
	}
}

static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;

	irq_set_pending(USB_LOW_PRIORITY_IRQ);

	return USB_TASK_INTERVAL_US;
}

static void key_cb(uint8_t key, enum key_state state)
{
	if (tud_hid_n_ready(USB_ITF_KEYBOARD) && reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON)) {
		uint8_t keycode[6] = { 0 };
		uint8_t modifiers = 0;

		if (state == KEY_STATE_PRESSED) {
			keycode[0] = key;
		}

		if (state != KEY_STATE_HOLD) {
			tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, modifiers, keycode);
		}
	}
}
static struct key_callback key_callback = { .func = key_cb };

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
	// TODO not Implemented
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)reqlen;

	return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t len)
{
	// TODO set LED based on CAPLOCK, NUMLOCK etc...
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)len;
}

void tud_vendor_rx_cb(uint8_t itf)
{
//	printf("%s: itf: %d, avail: %d\r\n", __func__, itf, tud_vendor_n_available(itf));

	uint8_t buff[64] = { 0 };
	tud_vendor_n_read(itf, buff, 64);
//	printf("%s: %02X %02X %02X\r\n", __func__, buff[0], buff[1], buff[2]);

	reg_process_packet(buff[0], buff[1], self.write_buffer, &self.write_len);

	tud_vendor_n_write(itf, self.write_buffer, self.write_len);
}

void tud_mount_cb(void)
{
	// Send mods over USB by default if USB connected
	reg_set_value(REG_ID_CFG, reg_get_value(REG_ID_CFG) | CFG_REPORT_MODS);
}

mutex_t *usb_get_mutex(void)
{
	return &self.mutex;
}

void usb_init(void)
{
	tusb_init();

	keyboard_add_key_callback(&key_callback);

	// create a new interrupt that calls tud_task, and trigger that interrupt from a timer
	irq_set_exclusive_handler(USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
	irq_set_enabled(USB_LOW_PRIORITY_IRQ, true);

	mutex_init(&self.mutex);
	add_alarm_in_us(USB_TASK_INTERVAL_US, timer_task, NULL, true);
}
