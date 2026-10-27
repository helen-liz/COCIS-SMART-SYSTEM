#ifndef SMART_SYSTEM_TYPES_H
#define SMART_SYSTEM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_ALLOWED_CARDS 32

typedef struct {
	uint8_t allowed_uids[MAX_ALLOWED_CARDS][4];
	uint8_t allowed_count;
	uint16_t schedule_start_minute;
	uint16_t schedule_end_minute;
	char admin_pin[9];
} system_config_t;

typedef struct {
	uint8_t room_id;
	const char *name;
	bool exit_node;
} room_profile_t;

#endif