#include "reg.h"

enum update_mode {
	UPDATE_OFF = 0,
	UPDATE_RECV = 1
};

void update_init();
int update_recv(uint8_t b);
