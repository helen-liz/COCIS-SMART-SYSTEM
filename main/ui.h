#ifndef SMART_SYSTEM_UI_H
#define SMART_SYSTEM_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "communication.h"
#include "system_types.h"

typedef struct {
	const char *room_name;
	uint8_t room_id;
	bool exit_node;
	bool entrance;
	bool authenticated;
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
	uint32_t entries;
	uint32_t exits;
	uint32_t uptime_seconds;
	size_t log_bytes;
	bool web_active;
	bool clock_set;
	time_t now;
	char last_event[64];
	bool last_event_ok;
	time_t last_event_time;
} ui_status_t;

void ui_set_color(bool enabled);
bool ui_color_enabled(void);
void ui_banner(const char *room_name, uint8_t room_id, bool entrance, bool exit_node);
void ui_help(void);
void ui_menu(void);
void ui_clear_screen(void);
void ui_print_status(const ui_status_t *status);
void ui_print_who(const inside_record_t *records, uint8_t count);
void ui_print_rooms(const remote_node_info_t *nodes, size_t count);
void ui_print_stats(const ui_status_t *status);
void ui_ok(const char *format, ...);
void ui_error(const char *format, ...);
void ui_warn(const char *format, ...);
void ui_info(const char *format, ...);
void ui_event(bool granted, const char *event, const char *uid, const char *detail,
			  const char *room);

#endif
