#include <stdio.h>
#include <string.h>

#include "display.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SSD1306_ADDRESS 0x3C
#define SSD1306_TIMEOUT_MS 50
#define DISPLAY_REFRESH_MS 500
#define EVENT_OVERLAY_MS 2500

static const char *TAG = "smart_display";

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t ssd1306;
static bool display_available;
static uint8_t framebuffer[8][DISPLAY_WIDTH];
static TickType_t overlay_until;
static bool overlay_active;
static TickType_t last_refresh;

static const uint8_t font5x7[95][5] = {
	{0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
	{0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
	{0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
	{0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
	{0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
	{0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
	{0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
	{0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
	{0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
	{0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
	{0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
	{0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
	{0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
	{0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
	{0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
	{0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
	{0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
	{0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
	{0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
	{0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
	{0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
	{0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
	{0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
	{0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
	{0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
	{0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
	{0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
	{0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
	{0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
	{0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
	{0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00},
	{0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
	{0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
	{0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
	{0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
	{0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
	{0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
	{0x20, 0x40, 0x44, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
	{0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
	{0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
	{0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
	{0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
	{0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
	{0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
	{0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
	{0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
	{0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
	{0x08, 0x04, 0x08, 0x10, 0x08},
};

static esp_err_t command(const uint8_t *values, size_t count)
{
	uint8_t buffer[32];
	if (count > sizeof(buffer) - 1) {
		return ESP_ERR_INVALID_SIZE;
	}
	buffer[0] = 0x00;
	memcpy(buffer + 1, values, count);
	return i2c_master_transmit(ssd1306, buffer, count + 1, SSD1306_TIMEOUT_MS);
}

static void set_pixel(int x, int y, bool on)
{
	if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
		return;
	}
	if (on) {
		framebuffer[y / 8][x] |= (uint8_t)(1u << (y % 8));
	} else {
		framebuffer[y / 8][x] &= (uint8_t)~(1u << (y % 8));
	}
}

static void fill_rect(int x, int y, int width, int height, bool on)
{
	for (int row = 0; row < height; ++row) {
		for (int column = 0; column < width; ++column) {
			set_pixel(x + column, y + row, on);
		}
	}
}

static void draw_hline(int x, int y, int width)
{
	for (int column = 0; column < width; ++column) {
		set_pixel(x + column, y, true);
	}
}

static int text_width(const char *text, uint8_t scale)
{
	return (int)strlen(text) * 6 * scale;
}

static void draw_char(int x, int y, char character, uint8_t scale, bool on)
{
	unsigned char code = (unsigned char)character;
	if (code < 32 || code > 126) {
		code = '?';
	}
	const uint8_t *glyph = font5x7[code - 32];
	for (int column = 0; column < 5; ++column) {
		uint8_t bits = glyph[column];
		for (int row = 0; row < 7; ++row) {
			if ((bits >> row) & 0x01) {
				fill_rect(x + column * scale, y + row * scale, scale, scale, on);
			}
		}
	}
}

static void draw_text(int x, int y, const char *text, uint8_t scale, bool on)
{
	int cursor = x;
	for (const char *character = text; *character != '\0'; ++character) {
		draw_char(cursor, y, *character, scale, on);
		cursor += 6 * scale;
	}
}

static void draw_text_clipped(int x, int y, const char *text, uint8_t scale, int max_x,
							  bool on)
{
	int cursor = x;
	for (const char *character = text; *character != '\0'; ++character) {
		if (cursor + 5 * scale > max_x) {
			break;
		}
		draw_char(cursor, y, *character, scale, on);
		cursor += 6 * scale;
	}
}

static void draw_text_right(int right, int y, const char *text, uint8_t scale, bool on)
{
	draw_text(right - text_width(text, scale), y, text, scale, on);
}

static void draw_text_center(int y, const char *text, uint8_t scale, bool on)
{
	draw_text((DISPLAY_WIDTH - text_width(text, scale)) / 2, y, text, scale, on);
}

static void draw_header(const char *right_text)
{
	fill_rect(0, 0, DISPLAY_WIDTH, 10, true);
	draw_text(2, 2, "COCIS SMART", 1, false);
	if (right_text != NULL) {
		draw_text_right(DISPLAY_WIDTH - 2, 2, right_text, 1, false);
	}
}

static void format_clock(const display_state_t *state, char *buffer, size_t size)
{
	if (!state->clock_set) {
		snprintf(buffer, size, "--:--");
		return;
	}
	struct tm local_time;
	if (localtime_r(&state->now, &local_time) == NULL) {
		snprintf(buffer, size, "--:--");
		return;
	}
	strftime(buffer, size, "%H:%M", &local_time);
}

static void draw_badge(int x, int y, const char *text, bool active)
{
	int width = text_width(text, 1) + 4;
	if (active) {
		fill_rect(x, y, width, 10, true);
		draw_text(x + 2, y + 2, text, 1, false);
	} else {
		fill_rect(x, y, width, 10, false);
		for (int column = 1; column < width - 1; ++column) {
			set_pixel(x + column, y, true);
			set_pixel(x + column, y + 9, true);
		}
		for (int row = 1; row < 9; ++row) {
			set_pixel(x, y + row, true);
			set_pixel(x + width - 1, y + row, true);
		}
		draw_text(x + 2, y + 2, text, 1, true);
	}
}

static void draw_dashboard(const display_state_t *state)
{
	memset(framebuffer, 0, sizeof(framebuffer));

	char clock_text[8];
	format_clock(state, clock_text, sizeof(clock_text));
	draw_header(clock_text);

	char room_line[32];
	snprintf(room_line, sizeof(room_line), "%s", state->room_name);
	draw_text_clipped(2, 13, room_line, 1, 86, true);
	if (state->entrance) {
		draw_text_right(DISPLAY_WIDTH - 2, 13, "IN/OUT", 1, true);
	} else {
		draw_text_right(DISPLAY_WIDTH - 2, 13, state->exit_node ? "EXIT" : "ROOM",
						1, true);
	}
	draw_hline(0, 22, DISPLAY_WIDTH);

	int32_t count = state->entrance ? state->global_occupancy
									: state->local_occupancy;
	if (count < 0) {
		count = 0;
	}
	if (count > 99) {
		count = 99;
	}
	char count_text[4];
	snprintf(count_text, sizeof(count_text), "%02d", (int)count);
	draw_text(2, 27, count_text, 3, true);

	if (state->entrance) {
		draw_text(44, 29, "PEOPLE", 1, true);
		draw_text(44, 39, "INSIDE", 1, true);
	} else {
		draw_text(44, 29, "ROOM", 1, true);
		draw_text(44, 39, state->motion ? "ACTIVE" : "IDLE", 1, true);
	}

	int badge_x = 82;
	draw_text(badge_x, 26, "ACCESS", 1, true);
	bool open = state->schedule_open || state->override_active;
	if (state->emergency) {
		draw_badge(badge_x, 35, "FIRE", true);
	} else if (state->lockdown) {
		draw_badge(badge_x, 35, "LOCK", true);
	} else {
		draw_badge(badge_x, 35, open ? "OPEN" : "SHUT", open);
	}
	draw_text_right(DISPLAY_WIDTH - 2, 48, state->light_on ? "LIGHT ON" : "LIGHT OFF", 1,
					true);

	draw_hline(0, 56, DISPLAY_WIDTH);
	if (state->last_event[0] != '\0') {
		fill_rect(1, 58, 3, 3, state->last_event_ok);
		draw_text_clipped(6, 58, state->last_event, 1, DISPLAY_WIDTH - 1, true);
	} else {
		draw_text(1, 58, "SYSTEM READY", 1, true);
	}

	if (state->last_event[0] == '\0') {
		draw_text_right(DISPLAY_WIDTH - 1, 58, "v2.0", 1, true);
	}
}

static void draw_event_overlay(bool granted, const char *headline, const char *uid_text,
							   const char *detail)
{
	memset(framebuffer, 0, sizeof(framebuffer));
	draw_header(granted ? "ACCESS OK" : "ACCESS DENIED");
	uint8_t headline_scale = text_width(headline, 2) <= DISPLAY_WIDTH - 4 ? 2 : 1;
	draw_text_center(13, headline, headline_scale, true);

	char line[32];
	snprintf(line, sizeof(line), "CARD %s", uid_text != NULL ? uid_text : "--------");
	draw_text_center(34, line, 1, true);

	if (detail != NULL) {
		char trimmed[24];
		snprintf(trimmed, sizeof(trimmed), "%.20s", detail);
		draw_text_center(46, trimmed, 1, true);
	}

	if (granted) {
		draw_badge((DISPLAY_WIDTH - text_width("GRANTED", 1) - 4) / 2, 54, "GRANTED",
				   true);
	} else {
		draw_badge((DISPLAY_WIDTH - text_width("DENIED", 1) - 4) / 2, 54, "DENIED", true);
	}
}

static esp_err_t flush(void)
{
	uint8_t buffer[DISPLAY_WIDTH + 1];
	buffer[0] = 0x40;
	for (uint8_t page = 0; page < 8; ++page) {
		uint8_t position[3] = {(uint8_t)(0xB0 | page), 0x00, 0x10};
		esp_err_t result = command(position, sizeof(position));
		if (result != ESP_OK) {
			return result;
		}
		memcpy(buffer + 1, framebuffer[page], DISPLAY_WIDTH);
		result = i2c_master_transmit(ssd1306, buffer, sizeof(buffer), SSD1306_TIMEOUT_MS);
		if (result != ESP_OK) {
			return result;
		}
	}
	return ESP_OK;
}

esp_err_t display_init(void)
{
	i2c_master_bus_config_t bus_config = {
		.i2c_port = I2C_NUM_0,
		.sda_io_num = SMART_SYSTEM_PIN_OLED_SDA,
		.scl_io_num = SMART_SYSTEM_PIN_OLED_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	esp_err_t result = i2c_new_master_bus(&bus_config, &i2c_bus);
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "I2C bus unavailable: %s", esp_err_to_name(result));
		return result;
	}

	i2c_device_config_t device_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = SSD1306_ADDRESS,
		.scl_speed_hz = 400000,
	};
	result = i2c_master_bus_add_device(i2c_bus, &device_config, &ssd1306);
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "SSD1306 not found: %s", esp_err_to_name(result));
		return result;
	}

	const uint8_t init_sequence[] = {
		0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40, 0x81, 0xCF,
		0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3, 0x00, 0xD5, 0x80, 0xD9,
		0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xAF,
	};
	result = command(init_sequence, sizeof(init_sequence));
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "SSD1306 init failed: %s", esp_err_to_name(result));
		return result;
	}
	display_available = true;
	memset(framebuffer, 0, sizeof(framebuffer));
	draw_text_center(24, "COCIS", 3, true);
	draw_text_center(46, "SMART SYSTEM", 1, true);
	flush();
	ESP_LOGI(TAG, "SSD1306 status display ready on SDA %d / SCL %d",
			 SMART_SYSTEM_PIN_OLED_SDA, SMART_SYSTEM_PIN_OLED_SCL);
	return ESP_OK;
}

bool display_ready(void)
{
	return display_available;
}

void display_render(const display_state_t *state)
{
	if (!display_available || overlay_active) {
		return;
	}
	draw_dashboard(state);
	if (flush() != ESP_OK) {
		ESP_LOGW(TAG, "Display refresh failed");
	}
	last_refresh = xTaskGetTickCount();
}

void display_show_event(bool granted, const char *headline, const char *uid_text,
						const char *detail)
{
	if (!display_available) {
		return;
	}
	draw_event_overlay(granted, headline, uid_text, detail);
	flush();
	overlay_active = true;
	overlay_until = xTaskGetTickCount() + pdMS_TO_TICKS(EVENT_OVERLAY_MS);
}

void display_tick(const display_state_t *state)
{
	if (!display_available) {
		return;
	}
	TickType_t now = xTaskGetTickCount();
	if (overlay_active && (TickType_t)(now - overlay_until) < 0x80000000u) {
		return;
	}
	if (overlay_active) {
		overlay_active = false;
		last_refresh = 0;
	}
	if (last_refresh == 0 ||
		(TickType_t)(now - last_refresh) >= pdMS_TO_TICKS(DISPLAY_REFRESH_MS)) {
		display_render(state);
	}
}

void display_force_redraw(void)
{
	overlay_active = false;
	last_refresh = 0;
}
