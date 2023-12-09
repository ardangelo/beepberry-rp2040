#include <string.h>

#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>

#include <flashloader.h>

#include "update.h"

// See https://github.com/rhulme/pico-flashloader/blob/master/app.c
struct hex_record
{
	uint8_t count;
	uint16_t addr;
	uint8_t type;
	uint8_t data[256];
};

#define FLASH_IMAGE_MAX_SIZE 65536

// Hex record types
static const uint8_t TYPE_DATA = 0x00;
static const uint8_t TYPE_EOF = 0x01;
static const uint8_t TYPE_EXTSEG = 0x02;
static const uint8_t TYPE_STARTSEG = 0x03;
static const uint8_t TYPE_EXTLIN = 0x04;
static const uint8_t TYPE_STARTLIN = 0x05;

// Offset within flash of the new app image to be flashed by the flashloader
static const uint32_t FLASH_IMAGE_OFFSET = 128 * 1024;

// Buffer to hold the incoming data before flashing
static union
{
	tFlashHeader header;
	uint8_t buf[sizeof(tFlashHeader) + FLASH_IMAGE_MAX_SIZE];
} flashbuf;

static uint32_t crc32(const uint8_t *data, uint32_t len, uint32_t crc)
{
	int bit;

	while (len--) {
		crc ^= (*data++ << 24);

		for (bit = 0; bit < 8; bit++) {
			if (crc & (1L << 31)) {
				crc = (crc << 1) ^ 0x04C11DB7;
			} else {
				crc = (crc << 1);
			}
		}
	}

	return crc;
}

static int hex2nibble(char c, uint8_t* value)
{
	if ((c >= '0') && (c <= '9')) {
		*value <<= 4;
		*value |= (uint8_t)(c - '0');
		return 0;

	} else {
		c |= 32;
		if ((c >= 'a') && (c <= 'z')) {
			*value <<= 4;
			*value |= (uint8_t)(c - 'a') + 10;
			return 0;
		}
	}

	return 1;
}

static int parse_2ch_hex(char const* str, uint8_t* value)
{
	*value = 0;
	return hex2nibble(*str++, value) || hex2nibble(*str, value);
}

// See https://github.com/rhulme/pico-flashloader/blob/master/app.c
static int process_hex_record(char const* line, struct hex_record* record)
{
	int rc = 0;

	uint8_t parsed_value = 0;
	uint8_t data[256 + 5]; // Max payload 256 bytes plus 5 for fields
	size_t data_offset = 0;
	uint8_t checksum = 0;

	while ((*line != '\0') && (*line != ':')) {
		line++;
	}

	if (*line == '\0') {
		return -UPDATE_FAILED_BAD_LINE;
	}

	// Advance past :
	line++;

	while ((*line != '\0') && (data_offset < sizeof(data))) {

		if ((line[1] == '\0')
		 || parse_2ch_hex(line, &parsed_value)) {
			return -UPDATE_FAILED_BAD_LINE;
		}

		data[data_offset] = parsed_value;
		data_offset++;
		checksum += parsed_value;
		line += 2;
	}

	// Checksum is two's-complement of the sum of the previous bytes so
	// final checksum should be zero if everything was OK.
	if (data_offset == 0) {
		return -UPDATE_FAILED_BAD_LINE;
	} else if (checksum != 0) {
		return -UPDATE_FAILED_BAD_CHECKSUM;
	}

	record->count = data[0];
	record->addr  = data[2] | (data[1] << 8);
	record->type  = data[3];
	memcpy(record->data, &data[4], data[0]);

	return 0;
}

// See https://github.com/rhulme/pico-flashloader/blob/master/flashloader.c
static void flash_image(tFlashHeader* header, uint32_t length)
{
	uint32_t total_length, erase_length, status;

	// Calculate length of header plus length of data
	total_length = sizeof(tFlashHeader) + length;

	// Round erase length up to next 4096 byte boundary
	erase_length = (total_length + 4095) & 0xfffff000;

	header->magic1 = FLASH_MAGIC1;
	header->magic2 = FLASH_MAGIC2;
	header->length = length;
	header->crc32  = crc32(header->data, length, 0xffffffff);

	status = save_and_disable_interrupts();

	flash_range_erase(FLASH_IMAGE_OFFSET, erase_length);
	flash_range_program(FLASH_IMAGE_OFFSET, (uint8_t*)header, total_length);

	restore_interrupts(status);

	// Set up watchdog scratch registers so that the flashloader knows
	// what to do after the reset
	watchdog_hw->scratch[0] = FLASH_MAGIC1;
	watchdog_hw->scratch[1] = XIP_BASE + FLASH_IMAGE_OFFSET;
	watchdog_reboot(0x00000000, 0x00000000, 1000);

	// Wait for the reset
	while (true) {
		tight_loop_contents();
	}
}

static char update_line[1024];
static size_t update_line_idx = 0;
static struct hex_record update_record;
static size_t flashbuf_offset = 0;

void update_init()
{
	update_line_idx = 0;
	flashbuf_offset = 0;
}

int update_recv(uint8_t b)
{
	int rc;

	// Check for line overflow
	if (update_line_idx == sizeof(update_line)) {
		return -UPDATE_FAILED_LINE_OVERFLOW;
	}

	// Check for line terminator
	if ((b == '\n') || (b == '\r')) {

		// Ignore empty line
		if (update_line_idx == 0) {
			return 1;
		}

		b = '\0';
	}

	// Set next character
	update_line[update_line_idx++] = b;

	// If line wasn't done, return to read more
	if (b) {
		return 1;
	}

	// Process incoming line
	if ((rc = process_hex_record(update_line, &update_record)) < 0) {
		return rc;
	}

	// Reset to beginning of line buffer
	update_line_idx = 0;
	update_line[0] = '\0';

	switch (update_record.type) {

	// Copy parsed updated data into flash buffer
	case TYPE_DATA:
		memcpy(&flashbuf.header.data[flashbuf_offset],
			update_record.data, update_record.count);
		flashbuf_offset += update_record.count;
		if (flashbuf_offset >= FLASH_IMAGE_MAX_SIZE) {
			return -UPDATE_FAILED_FLASH_OVERFLOW;
		}
		return 1;

	// Complete firmware received
	case TYPE_EOF:
		if (flashbuf_offset == 0) {
			return -UPDATE_FAILED_FLASH_EMPTY;
		}

		return 0;

	// Reset flash buffer
	case TYPE_EXTLIN:
		flashbuf_offset = 0;
		return 1;

	// Ignore
	case TYPE_EXTSEG:
	case TYPE_STARTSEG:
	case TYPE_STARTLIN:
	default:
		return 1;
	}
}

void update_commit_and_reboot(void)
{
	flash_image(&flashbuf.header, flashbuf_offset);
}
