#include "reg.h"

// Parse and write Intel hex format firmware
// Reboots on success to apply new firmware
// Returns on failure
void update_firmware_hex(char const* hex, size_t len);
