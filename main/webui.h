#ifndef SMART_SYSTEM_WEBUI_H
#define SMART_SYSTEM_WEBUI_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
	char room_name[32];
	uint8_t room_id;
	bool entrance;
	bool clock_set;
	time_t now;
	int32_t local_occupancy;
	int32_t global_occupancy;
	uint8_t inside_count;
	uint8_t allowed_count;
	uint16_t schedule_start_minute;
	uint16_t schedule_end_minute;
	bool schedule_open;
	bool override_active;
	bool lockdown;
	bool emergency;
	bool light_on;
	bool motion;
	uint32_t grants;
	uint32_t denials;
	char last_event[64];
	bool last_event_ok;
	time_t last_event_time;
} web_status_t;

void webui_start(uint8_t room_id);
bool webui_active(void);
void webui_update_status(const web_status_t *status);

#endif
