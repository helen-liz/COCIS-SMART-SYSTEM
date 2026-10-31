#ifndef SMART_SYSTEM_DISPLAY_H
#define SMART_SYSTEM_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifndef SMART_SYSTEM_PIN_OLED_SDA
#define SMART_SYSTEM_PIN_OLED_SDA 21
#endif
#ifndef SMART_SYSTEM_PIN_OLED_SCL
#define SMART_SYSTEM_PIN_OLED_SCL 25
#endif

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

typedef struct {
	const char *room_name;
	uint8_t room_id;
	bool exit_node;
	bool entrance;
	bool clock_set;
	time_t now;
	int32_t local_occupancy;
	int32_t global_occupancy;
	uint8_t inside_count;
	uint16_t schedule_start_minute;
	uint16_t schedule_end_minute;
	bool schedule_open;
	bool lockdown;
	bool emergency;
	bool light_on;
	bool motion;
	bool override_active;
	uint32_t grants;
	uint32_t denials;
	char last_event[64];
	bool last_event_ok;
} display_state_t;

esp_err_t display_init(void);
bool display_ready(void);
void display_render(const display_state_t *state);
void display_show_event(bool granted, const char *headline, const char *uid_text,
						const char *detail);
void display_tick(const display_state_t *state);
void display_force_redraw(void);

#endif
