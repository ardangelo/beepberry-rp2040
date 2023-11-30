#pragma once

#include "input-event-codes.h"

#include <stdbool.h>
#include <stdint.h>

enum key_state
{
	KEY_STATE_IDLE = 0,
	KEY_STATE_PRESSED = 1,
	KEY_STATE_HOLD = 2,
	KEY_STATE_RELEASED = 3,
	KEY_STATE_LONG_HOLD = 4,
};

#define LONG_HOLD_MS    5000

struct key_callback
{
	void (*func)(uint8_t key, enum key_state state);
	struct key_callback *next;
};

void keyboard_inject_event(uint8_t key, enum key_state state);
void keyboard_inject_power_key();

void keyboard_add_key_callback(struct key_callback *callback);

void keyboard_init(void);
