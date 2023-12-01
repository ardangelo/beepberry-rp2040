#include "app_config.h"
#include "fifo.h"
#include "keyboard.h"
#include "reg.h"
#include "pi.h"

#include <pico/stdlib.h>

// Size of the list keeping track of all the pressed keys
#define MAX_TRACKED_KEYS 10

static struct
{
	struct key_callback *key_callbacks;
} self;

// Key and buttons definitions

static const uint8_t row_pins[NUM_OF_ROWS] =
{
	PINS_ROWS
};

static const uint8_t col_pins[NUM_OF_COLS] =
{
	PINS_COLS
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const uint8_t kbd_entries[NUM_OF_ROWS][NUM_OF_COLS] =
//  Touchpad center key
{ { KEY_COMPOSE, KEY_W, KEY_G, KEY_S, KEY_L, KEY_H }
, {         0x0, KEY_Q, KEY_R, KEY_E, KEY_O, KEY_U }
//     Call button
, {    KEY_OPEN, KEY_0, KEY_F, KEY_LEFTSHIFT, KEY_K, KEY_J }
, {         0x0, KEY_SPACE, KEY_C, KEY_Z, KEY_M, KEY_N }
//    Berry key  Symbol key
, {   KEY_PROPS, KEY_RIGHTALT, KEY_T, KEY_D, KEY_I, KEY_Y }
//      Back key Alt key
, {     KEY_ESC, KEY_LEFTALT, KEY_V, KEY_X, KEY_MUTE, KEY_B }
, {         0x0, KEY_A, KEY_RIGHTSHIFT, KEY_P, KEY_BACKSPACE, KEY_ENTER }
};
static bool kbd_pressed_state[NUM_OF_ROWS][NUM_OF_COLS] = {};

#if NUM_OF_BTNS > 0

// Call end key mapped to GPIO 4
static const char btn_entries[NUM_OF_BTNS] = { KEY_POWER };
static const uint8_t btn_pins[NUM_OF_BTNS] = { 4 };
#endif

#pragma GCC diagnostic pop

struct hold_key
{
	uint8_t keycode;
	enum key_state state;
	uint hold_start_time;
};

static struct hold_key power_hold_key;
static struct hold_key left_shift_hold_key;
static struct hold_key right_shift_hold_key;
static struct hold_key phys_alt_hold_key;
static struct hold_key sym_hold_key;

static int64_t release_power_key_alarm_callback(alarm_id_t _, void* __)
{
	keyboard_inject_event(KEY_POWER, KEY_STATE_RELEASED);

	return 0;
}

static void transition_hold_key_state(struct hold_key* hold_key, bool const pressed)
{
	uint key_held_for;

	switch (hold_key->state) {

		// Idle -> Pressed
		case KEY_STATE_IDLE:
			if (pressed) {
				hold_key->state = KEY_STATE_PRESSED;

				// Track hold time for transitioning to Hold and Long Hold states
				hold_key->hold_start_time = to_ms_since_boot(get_absolute_time());
			}
			break;

		// Pressed -> Hold | Released
		case KEY_STATE_PRESSED:
			key_held_for = to_ms_since_boot(get_absolute_time())
				- hold_key->hold_start_time;
			if (key_held_for > (reg_get_value(REG_ID_HLD) * 10)) {
				hold_key->state = KEY_STATE_HOLD;

			} else if (!pressed) {
				hold_key->state = KEY_STATE_RELEASED;
			}
			break;

		// Hold -> Released | Long Hold
		case KEY_STATE_HOLD:
			if (!pressed) {
				hold_key->state = KEY_STATE_RELEASED;

			// Power key can be long hold
			} else if (hold_key->keycode == KEY_POWER) {

				// Driver unloaded, power back on
				if (reg_get_value(REG_ID_DRIVER_STATE) == 0) {
					pi_power_on(POWER_ON_BUTTON);
					hold_key->state = KEY_STATE_LONG_HOLD;

				// Driver loaded, send power off
				} else {

					// Check for long hold time
					key_held_for = to_ms_since_boot(get_absolute_time())
						- hold_key->hold_start_time;
					if (key_held_for > LONG_HOLD_MS) {

						keyboard_inject_power_key();

						// Schedule power off
						uint32_t shutdown_grace_ms = MAX(
							reg_get_value(REG_ID_SHUTDOWN_GRACE) * 1000,
							MINIMUM_SHUTDOWN_GRACE_MS);
						pi_schedule_power_off(shutdown_grace_ms);

						hold_key->state = KEY_STATE_LONG_HOLD;
					}
				}
			}
			break;

		// Long Hold -> Released
		case KEY_STATE_LONG_HOLD:
			if (!pressed) {
				hold_key->state = KEY_STATE_RELEASED;
			}
			break;

		// Released -> Idle
		case KEY_STATE_RELEASED:
			hold_key->state = KEY_STATE_IDLE;
			break;
	}
}

// Return whether key event should be sent to queue
static bool handle_hold_key_event(struct hold_key* hold_key, enum key_state* state,
	bool pressed)
{
	// Save previous state
	*state = hold_key->state;

	// Transition state
	transition_hold_key_state(hold_key, pressed);

	// Compare to previous state
	if (*state != hold_key->state) {
		*state = hold_key->state;

		// Only send pressed, released, or hold events
		return (*state == KEY_STATE_PRESSED)
			|| (*state == KEY_STATE_RELEASED)
			|| (*state == KEY_STATE_HOLD);
	}

	return false;
}

static void handle_key_event(uint r, uint c, bool pressed)
{
	uint8_t keycode;
	bool send_update = false;
	enum key_state state;

	// Get keycode
	keycode = kbd_entries[r][c];

	// Don't send power key over USB
	if ((keycode == 0) || (keycode == KEY_POWER)) {
		return;
	}

	// Handle holdable modifiers
	if (keycode == left_shift_hold_key.keycode) {
		send_update = handle_hold_key_event(&left_shift_hold_key, &state, pressed);

	} else if (keycode == left_shift_hold_key.keycode) {
		send_update = handle_hold_key_event(&left_shift_hold_key, &state, pressed);

	} else if (keycode == phys_alt_hold_key.keycode) {
		send_update = handle_hold_key_event(&phys_alt_hold_key, &state, pressed);

	} else if (keycode == sym_hold_key.keycode) {
		send_update = handle_hold_key_event(&sym_hold_key, &state, pressed);

	// Basic press / release alpha key
	} else {
		send_update = (kbd_pressed_state[r][c] != pressed);
		kbd_pressed_state[r][c] = pressed;
		state = (pressed) ? KEY_STATE_PRESSED : KEY_STATE_RELEASED;
	}

	// Don't send duplicate key events
	if (!send_update) {
		return;
	}

	// Report key to input system
	keyboard_inject_event(keycode, state);
}

static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;
	uint c, r, i;
	bool pressed;

	for (c = 0; c < NUM_OF_COLS; c++) {
		gpio_pull_up(col_pins[c]);
		gpio_put(col_pins[c], 0);
		gpio_set_dir(col_pins[c], GPIO_OUT);

		for (r = 0; r < NUM_OF_ROWS; r++) {
			pressed = (gpio_get(row_pins[r]) == 0);
			handle_key_event(r, c, pressed);
		}

		gpio_put(col_pins[c], 1);
		gpio_disable_pulls(col_pins[c]);
		gpio_set_dir(col_pins[c], GPIO_IN);
	}

#if NUM_OF_BTNS > 0
	for (i = 0; i < NUM_OF_BTNS; i++) {
		pressed = (gpio_get(btn_pins[i]) == 0);
		transition_hold_key_state(&power_hold_key, pressed);
	}
#endif

	// negative value means interval since last alarm time
	return -(reg_get_value(REG_ID_FRQ) * 1000);
}

void keyboard_inject_event(uint8_t key, enum key_state state)
{
	struct fifo_item item;
	item.scancode = key;
	item.state = state;

	if (!fifo_enqueue(item)) {
		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_INT)) {
			reg_set_bit(REG_ID_INT, INT_OVERFLOW);
		}

		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_ON)) {
			fifo_enqueue_force(item);
		}
	}

	struct key_callback *cb = self.key_callbacks;
	while (cb) {
		cb->func(key, state);
		cb = cb->next;
	}
}

// Simulate press event and schedule release
void keyboard_inject_power_key()
{
	keyboard_inject_event(KEY_POWER, KEY_STATE_PRESSED);
	add_alarm_in_ms(10, release_power_key_alarm_callback, NULL, true);
}

void keyboard_add_key_callback(struct key_callback *callback)
{
	// first callback
	if (!self.key_callbacks) {
		self.key_callbacks = callback;
		return;
	}

	// find last and insert after
	struct key_callback *cb = self.key_callbacks;
	while (cb->next) {
		cb = cb->next;
	}
	cb->next = callback;
}

void keyboard_init(void)
{
	uint i;

	// GPIO rows
	for (i = 0; i < NUM_OF_ROWS; ++i) {
		gpio_init(row_pins[i]);
		gpio_pull_up(row_pins[i]);
		gpio_set_dir(row_pins[i], GPIO_IN);
	}

	// GPIO columns
	for(i = 0; i < NUM_OF_COLS; ++i) {
		gpio_init(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
	}

	// GPIO buttons
#if NUM_OF_BTNS > 0
	for(i = 0; i < NUM_OF_BTNS; ++i) {
		gpio_init(btn_pins[i]);
		gpio_pull_up(btn_pins[i]);
		gpio_set_dir(btn_pins[i], GPIO_IN);
	}
#endif

	// Holdable modfiier keys
	power_hold_key.keycode = KEY_POWER;
	power_hold_key.state = KEY_STATE_IDLE;
	left_shift_hold_key.keycode = KEY_LEFTSHIFT;
	left_shift_hold_key.state = KEY_STATE_IDLE;
	right_shift_hold_key.keycode = KEY_RIGHTSHIFT;
	right_shift_hold_key.state = KEY_STATE_IDLE;
	phys_alt_hold_key.keycode = KEY_LEFTALT;
	phys_alt_hold_key.state = KEY_STATE_IDLE;
	sym_hold_key.keycode = KEY_RIGHTALT;
	sym_hold_key.state = KEY_STATE_IDLE;

	add_alarm_in_ms(reg_get_value(REG_ID_FRQ), timer_task, NULL, true);
}
