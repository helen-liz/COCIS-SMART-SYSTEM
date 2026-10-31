#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "communication.h"
#include "display.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "room_profiles.h"
#include "storage.h"
#include "ui.h"
#include "webui.h"

#ifndef SMART_SYSTEM_SPI_HOST
#define SMART_SYSTEM_SPI_HOST SPI2_HOST
#endif
#ifndef SMART_SYSTEM_PIN_SCK
#define SMART_SYSTEM_PIN_SCK 18
#endif
#ifndef SMART_SYSTEM_PIN_MOSI
#define SMART_SYSTEM_PIN_MOSI 23
#endif
#ifndef SMART_SYSTEM_PIN_MISO
#define SMART_SYSTEM_PIN_MISO 19
#endif
#ifndef SMART_SYSTEM_PIN_RFID_ENTRY_CS
#define SMART_SYSTEM_PIN_RFID_ENTRY_CS 5
#endif
#ifndef SMART_SYSTEM_PIN_RFID_ENTRY_RST
#define SMART_SYSTEM_PIN_RFID_ENTRY_RST 22
#endif
#ifndef SMART_SYSTEM_PIN_RFID_EXIT_CS
#define SMART_SYSTEM_PIN_RFID_EXIT_CS 4
#endif
#ifndef SMART_SYSTEM_PIN_RFID_EXIT_RST
#define SMART_SYSTEM_PIN_RFID_EXIT_RST 13
#endif
#ifndef SMART_SYSTEM_PIN_DOOR_ENTRY_RELAY
#define SMART_SYSTEM_PIN_DOOR_ENTRY_RELAY 26
#endif
#ifndef SMART_SYSTEM_PIN_DOOR_EXIT_RELAY
#define SMART_SYSTEM_PIN_DOOR_EXIT_RELAY 33
#endif
#ifndef SMART_SYSTEM_PIN_LIGHT_RELAY
#define SMART_SYSTEM_PIN_LIGHT_RELAY 27
#endif
#ifndef SMART_SYSTEM_PIN_PIR
#define SMART_SYSTEM_PIN_PIR 32
#endif

#define RFID_SPI_CLOCK_HZ 1000000
#define PIR_LIGHT_HOLD_MS 30000
#define DOOR_PULSE_MS 700
#define CARD_COOLDOWN_MS 3000
#define CLOCK_SENTINEL 1760000000
#define LOGIN_MAX_FAILURES 3
#define LOGIN_LOCK_SECONDS 60

static const char *TAG = "smart_system";

#if SMART_SYSTEM_ROOM_PROFILE == 0
static const room_profile_t *const active_profile = &ENTRANCE_PROFILE;
#elif SMART_SYSTEM_ROOM_PROFILE == 2
static const room_profile_t *const active_profile = &ROOM2_PROFILE;
#else
static const room_profile_t *const active_profile = &ROOM1_PROFILE;
#endif

enum {
	RC522_COMMAND_REG = 0x01,
	RC522_COM_IRQ_REG = 0x04,
	RC522_ERROR_REG = 0x06,
	RC522_FIFO_DATA_REG = 0x09,
	RC522_FIFO_LEVEL_REG = 0x0A,
	RC522_CONTROL_REG = 0x0C,
	RC522_BIT_FRAMING_REG = 0x0D,
	RC522_MODE_REG = 0x11,
	RC522_TX_CONTROL_REG = 0x14,
	RC522_TX_ASK_REG = 0x15,
	RC522_VERSION_REG = 0x37,
	RC522_SOFT_RESET = 0x0F,
	RC522_TRANSCEIVE = 0x0C
};

typedef struct {
	spi_device_handle_t spi;
	gpio_num_t cs_pin;
	gpio_num_t rst_pin;
	bool exit_section;
	bool ready;
	uint8_t last_uid[4];
	TickType_t last_seen;
} rfid_reader_t;

static rfid_reader_t entry_reader;
static rfid_reader_t exit_reader;
static bool exit_reader_present;

static system_config_t config;
static SemaphoreHandle_t state_mutex;
static int32_t local_occupancy;
static inside_record_t inside_records[MAX_INSIDE_RECORDS];
static uint8_t inside_count;
static TickType_t last_motion_tick;
static bool motion_hold_active;
static TickType_t light_hold_until;
static TickType_t boot_tick;
static TickType_t override_until;
static TickType_t enroll_until;
static volatile bool enroll_mode;
static bool lockdown_active;
static bool emergency_active;
static uint32_t granted_total;
static uint32_t denied_total;
static uint32_t entry_total;
static uint32_t exit_total;
static char last_event[64];
static bool last_event_ok;
static time_t last_event_time;

static void state_lock(void)
{
	xSemaphoreTake(state_mutex, portMAX_DELAY);
}

static void state_unlock(void)
{
	xSemaphoreGive(state_mutex);
}

static bool tick_pending(TickType_t deadline)
{
	return (int32_t)(deadline - xTaskGetTickCount()) > 0;
}

static esp_err_t rc522_write(rfid_reader_t *reader, uint8_t reg, uint8_t value)
{
	uint8_t tx[2] = {(uint8_t)((reg << 1) & 0x7E), value};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
	};
	return spi_device_transmit(reader->spi, &transaction);
}

static esp_err_t rc522_read(rfid_reader_t *reader, uint8_t reg, uint8_t *value)
{
	uint8_t tx[2] = {(uint8_t)(((reg << 1) & 0x7E) | 0x80), 0};
	uint8_t rx[2] = {0};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
		.rx_buffer = rx,
	};
	esp_err_t result = spi_device_transmit(reader->spi, &transaction);
	if (result == ESP_OK) {
		*value = rx[1];
	}
	return result;
}

static bool rc522_transceive(rfid_reader_t *reader, const uint8_t *send_data,
							 size_t send_length, uint8_t *receive_data,
							 size_t *receive_length, uint8_t *last_bits)
{
	uint8_t value;
	if (rc522_write(reader, RC522_COM_IRQ_REG, 0x7F) != ESP_OK ||
		rc522_write(reader, RC522_FIFO_LEVEL_REG, 0x80) != ESP_OK ||
		rc522_write(reader, RC522_COMMAND_REG, 0x00) != ESP_OK) {
		return false;
	}
	for (size_t index = 0; index < send_length; ++index) {
		if (rc522_write(reader, RC522_FIFO_DATA_REG, send_data[index]) != ESP_OK) {
			return false;
		}
	}
	if (rc522_write(reader, RC522_COMMAND_REG, RC522_TRANSCEIVE) != ESP_OK ||
		rc522_write(reader, RC522_BIT_FRAMING_REG, 0x80) != ESP_OK) {
		return false;
	}

	/* Wait for Wokwi RC522 to populate FIFO â€” poll up to 30ms */
	uint8_t fifo_level = 0;
	for (unsigned attempt = 0; attempt < 30; ++attempt) {
		vTaskDelay(pdMS_TO_TICKS(1));
		if (rc522_read(reader, RC522_FIFO_LEVEL_REG, &fifo_level) != ESP_OK) {
			return false;
		}
		if (fifo_level > 0) {
			break;
		}
	}
	rc522_write(reader, RC522_BIT_FRAMING_REG, 0x00);
	if (fifo_level == 0) {
		return false;
	}
	uint8_t err_val = 0;
	rc522_read(reader, RC522_ERROR_REG, &err_val);
	if (err_val & 0x1B) {
		return false;
	}
	size_t available = fifo_level;
	if (available > *receive_length) {
		return false;
	}
	for (size_t index = 0; index < available; ++index) {
		if (rc522_read(reader, RC522_FIFO_DATA_REG, &receive_data[index]) != ESP_OK) {
			return false;
		}
	}
	*receive_length = available;
	if (last_bits != NULL && rc522_read(reader, RC522_CONTROL_REG, &value) == ESP_OK) {
		*last_bits = value & 0x07;
	}
	return true;
}

static bool rc522_read_uid(rfid_reader_t *reader, uint8_t uid[4])
{
	/* Skip REQA â€” Wokwi RC522 populates FIFO directly on anticollision */
	const uint8_t anticollision[] = {0x93, 0x20};
	uint8_t response[5];
	size_t response_length = sizeof(response);
	if (rc522_write(reader, RC522_BIT_FRAMING_REG, 0x00) != ESP_OK ||
		!rc522_transceive(reader, anticollision, sizeof(anticollision), response,
						  &response_length, NULL) ||
		response_length != 5 ||
		(uint8_t)(response[0] ^ response[1] ^ response[2] ^ response[3]) != response[4]) {
		return false;
	}
	memcpy(uid, response, 4);
	return true;
}

static bool rc522_init(rfid_reader_t *reader, gpio_num_t cs_pin, gpio_num_t rst_pin,
					   bool exit_section, bool required)
{
	reader->cs_pin = cs_pin;
	reader->rst_pin = rst_pin;
	reader->exit_section = exit_section;
	reader->ready = false;

	spi_device_interface_config_t device_config = {
		.clock_speed_hz = RFID_SPI_CLOCK_HZ,
		.mode = 0,
		.spics_io_num = cs_pin,
		.queue_size = 1,
	};
	if (spi_bus_add_device(SMART_SYSTEM_SPI_HOST, &device_config, &reader->spi) !=
		ESP_OK) {
		ESP_LOGW(TAG, "Could not attach RFID reader on CS %d", cs_pin);
		return false;
	}

	gpio_config_t reset_gpio = {
		.pin_bit_mask = 1ULL << rst_pin,
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&reset_gpio));
	gpio_set_level(rst_pin, 0);
	vTaskDelay(pdMS_TO_TICKS(20));
	gpio_set_level(rst_pin, 1);
	vTaskDelay(pdMS_TO_TICKS(50));

	if (rc522_write(reader, RC522_COMMAND_REG, RC522_SOFT_RESET) != ESP_OK) {
		goto failed;
	}
	vTaskDelay(pdMS_TO_TICKS(50));
	if (rc522_write(reader, 0x2A, 0x8D) != ESP_OK ||
		rc522_write(reader, 0x2B, 0x3E) != ESP_OK ||
		rc522_write(reader, 0x2D, 30) != ESP_OK ||
		rc522_write(reader, 0x2C, 0) != ESP_OK ||
		rc522_write(reader, RC522_TX_ASK_REG, 0x40) != ESP_OK ||
		rc522_write(reader, RC522_MODE_REG, 0x3D) != ESP_OK) {
		goto failed;
	}
	uint8_t tx_control;
	if (rc522_read(reader, RC522_TX_CONTROL_REG, &tx_control) == ESP_OK &&
		(tx_control & 0x03) != 0x03) {
		rc522_write(reader, RC522_TX_CONTROL_REG, tx_control | 0x03);
	}
	uint8_t version = 0;
	esp_err_t version_result = ESP_FAIL;
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		version_result = rc522_read(reader, RC522_VERSION_REG, &version);
		if (version_result == ESP_OK && (version == 0x91 || version == 0x92)) {
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
	if (version_result != ESP_OK || (version != 0x91 && version != 0x92)) {
		ESP_LOGW(TAG,
				 "RFID %s reader on CS %d not responding (version 0x%02X, expected 0x91/0x92); check 3.3V, SPI wiring and CS",
				 exit_section ? "exit" : "entry", cs_pin, version);
		goto failed;
	}
	reader->ready = true;
	ESP_LOGI(TAG, "RFID %s reader ready on CS %d (version 0x%02X)",
			 exit_section ? "exit" : "entry", cs_pin, version);
	return true;

failed:
	reader->ready = false;
	if (required) {
		ESP_LOGE(TAG, "RFID reader on CS %d failed to initialize", cs_pin);
	}
	return false;
}

static bool access_window_open_locked(void)
{
	time_t now = time(NULL);
	if (now < CLOCK_SENTINEL) {
		return false;
	}
	struct tm local_time;
	if (localtime_r(&now, &local_time) == NULL) {
		return false;
	}
	uint16_t minute = (uint16_t)(local_time.tm_hour * 60 + local_time.tm_min);
	uint16_t schedule_start = config.schedule_start_minute;
	uint16_t schedule_end = config.schedule_end_minute;
	return minute >= schedule_start && minute < schedule_end;
}

static bool override_active_locked(void)
{
	return tick_pending(override_until);
}

static bool uid_is_allowed_locked(const uint8_t uid[4])
{
	bool allowed = false;
	for (uint8_t index = 0; index < config.allowed_count; ++index) {
		if (memcmp(uid, config.allowed_uids[index], 4) == 0) {
			allowed = true;
			break;
		}
	}
	return allowed;
}

static bool inside_contains_locked(const uint8_t uid[4])
{
	for (uint8_t index = 0; index < MAX_INSIDE_RECORDS; ++index) {
		if (inside_records[index].valid &&
			memcmp(inside_records[index].uid, uid, 4) == 0) {
			return true;
		}
	}
	return false;
}

static bool inside_add_locked(const uint8_t uid[4])
{
	for (uint8_t index = 0; index < MAX_INSIDE_RECORDS; ++index) {
		if (!inside_records[index].valid) {
			memcpy(inside_records[index].uid, uid, 4);
			inside_records[index].entered_at = (uint32_t)time(NULL);
			inside_records[index].valid = true;
			++inside_count;
			return true;
		}
	}
	return false;
}

static bool inside_remove_locked(const uint8_t uid[4])
{
	for (uint8_t index = 0; index < MAX_INSIDE_RECORDS; ++index) {
		if (inside_records[index].valid &&
			memcmp(inside_records[index].uid, uid, 4) == 0) {
			memset(&inside_records[index], 0, sizeof(inside_records[index]));
			if (inside_count > 0) {
				--inside_count;
			}
			return true;
		}
	}
	return false;
}

static void set_last_event_locked(const char *event, bool ok)
{
	snprintf(last_event, sizeof(last_event), "%s", event);
	last_event_ok = ok;
	last_event_time = time(NULL);
}

static void update_presence(void)
{
	if (active_profile->room_id == 0) {
		motion_hold_active = false;
		return;
	}
	TickType_t now = xTaskGetTickCount();
	if (gpio_get_level(SMART_SYSTEM_PIN_PIR) != 0) {
		last_motion_tick = now;
		motion_hold_active = true;
	} else if (motion_hold_active &&
			   (TickType_t)(now - last_motion_tick) >=
				   pdMS_TO_TICKS(PIR_LIGHT_HOLD_MS)) {
		motion_hold_active = false;
	}
}

static void update_lighting(void)
{
	if (active_profile->room_id == 0) {
		return;
	}
	bool on = motion_hold_active || tick_pending(light_hold_until);
	gpio_set_level(SMART_SYSTEM_PIN_LIGHT_RELAY, on ? 1 : 0);
}

static void pulse_relay(gpio_num_t pin)
{
	gpio_set_level(pin, 1);
	vTaskDelay(pdMS_TO_TICKS(DOOR_PULSE_MS));
	gpio_set_level(pin, 0);
}

static void format_uid_text(const uint8_t uid[4], char *buffer, size_t size)
{
	snprintf(buffer, size, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
}

static uint32_t uptime_seconds(void)
{
	return (uint32_t)((xTaskGetTickCount() - boot_tick) * portTICK_PERIOD_MS / 1000);
}

static void handle_card(rfid_reader_t *reader, const uint8_t uid[4])
{
	bool is_exit = reader->exit_section;
	bool is_entrance = active_profile->room_id == 0;
	char uid_text[12];
	format_uid_text(uid, uid_text, sizeof(uid_text));

	bool enroll = false;
	bool granted = false;
	bool denied = false;
	bool pulse_door = false;
	const char *event_name = "";
	const char *detail = "";
	gpio_num_t door_pin =
		(is_entrance && is_exit) ? SMART_SYSTEM_PIN_DOOR_EXIT_RELAY
								 : SMART_SYSTEM_PIN_DOOR_ENTRY_RELAY;
	int32_t occupancy_after = 0;
	bool config_changed = false;

	state_lock();
	enroll = enroll_mode;
	if (enroll) {
		enroll_mode = false;
		if (!uid_is_allowed_locked(uid) && config.allowed_count < MAX_ALLOWED_CARDS) {
			memcpy(config.allowed_uids[config.allowed_count++], uid, 4);
			config_changed = true;
			granted = true;
			event_name = "ENROLL";
			detail = "card-added-to-allowlist";
			++granted_total;
		} else {
			denied = true;
			event_name = "ENROLL";
			detail = "card-already-enrolled";
		}
		occupancy_after = local_occupancy;
	} else if (!uid_is_allowed_locked(uid)) {
		denied = true;
		event_name = "DENIED";
		detail = "unknown-card";
	} else {
		bool schedule_open = access_window_open_locked();
		bool override_active = override_active_locked();
		bool emergency = emergency_active;
		bool lockdown = lockdown_active;

		if (lockdown && !is_exit) {
			denied = true;
			event_name = "DENIED";
			detail = "lockdown-active";
		} else if (!is_exit && !schedule_open && !override_active && !emergency) {
			denied = true;
			event_name = "DENIED";
			detail = "outside-schedule";
		} else if (is_entrance && !is_exit && inside_contains_locked(uid)) {
			denied = true;
			event_name = "DENIED";
			detail = "already-inside";
		} else if (is_entrance && !is_exit && inside_count >= MAX_INSIDE_RECORDS) {
			denied = true;
			event_name = "DENIED";
			detail = "inside-list-full";
		} else if (is_entrance && is_exit && !inside_contains_locked(uid)) {
			denied = true;
			event_name = "DENIED";
			detail = "not-inside";
		} else {
			granted = true;
			pulse_door = true;
			if (is_entrance) {
				if (is_exit) {
					inside_remove_locked(uid);
					local_occupancy = inside_count;
					++exit_total;
					event_name = "EXIT";
					detail = "occupancy-decremented";
				} else {
					inside_add_locked(uid);
					local_occupancy = inside_count;
					++entry_total;
					event_name = "ENTRY";
					detail = "occupancy-incremented";
				}
			} else if (is_exit) {
				if (local_occupancy > 0) {
					--local_occupancy;
				}
				++exit_total;
				event_name = "EXIT";
				detail = "room-occupancy-decremented";
			} else {
				++local_occupancy;
				++entry_total;
				event_name = "ENTRY";
				detail = "room-occupancy-incremented";
			}
			++granted_total;
		}
		if (denied) {
			++denied_total;
		}
		occupancy_after = local_occupancy;
	}

	char event_line[64];
	snprintf(event_line, sizeof(event_line), "%s %s %s", event_name, uid_text,
			 granted ? "GRANTED" : "DENIED");
	set_last_event_locked(event_line, granted);
	state_unlock();

	if (config_changed) {
		storage_save_config(&config);
	}
	if (pulse_door) {
		pulse_relay(door_pin);
		if (is_entrance) {
			storage_save_inside(inside_records, inside_count);
		}
		if (!is_entrance) {
			light_hold_until =
				xTaskGetTickCount() + pdMS_TO_TICKS(PIR_LIGHT_HOLD_MS);
		}
		storage_save_occupancy(occupancy_after);
		update_lighting();
	}
	storage_write_event(active_profile, event_name, uid, detail);

	if (enroll) {
		if (granted) {
			display_show_event(true, "CARD ENROLLED", uid_text, detail);
			ui_ok("Card %s added to the allowlist.", uid_text);
		} else {
			display_show_event(false, "ENROL FAILED", uid_text, detail);
			ui_warn("Card %s is already enrolled.", uid_text);
		}
	} else if (granted) {
		display_show_event(true, is_exit ? "EXIT GRANTED" : "ACCESS GRANTED", uid_text,
						   detail);
		ui_event(true, event_name, uid_text, detail, active_profile->name);
	} else {
		display_show_event(false, "ACCESS DENIED", uid_text, detail);
		ui_event(false, event_name, uid_text, detail, active_profile->name);
	}
}

static void poll_reader(rfid_reader_t *reader)
{
	if (!reader->ready) {
		return;
	}
	TickType_t now = xTaskGetTickCount();
	uint8_t uid[4];
	if (rc522_read_uid(reader, uid)) {
		if (memcmp(uid, reader->last_uid, sizeof(uid)) != 0 ||
			(TickType_t)(now - reader->last_seen) > pdMS_TO_TICKS(CARD_COOLDOWN_MS)) {
			memcpy(reader->last_uid, uid, sizeof(reader->last_uid));
			reader->last_seen = now;
			handle_card(reader, uid);
		}
	} else if ((TickType_t)(now - reader->last_seen) > pdMS_TO_TICKS(500)) {
		memset(reader->last_uid, 0, sizeof(reader->last_uid));
	}
}

static void build_display_state(display_state_t *state)
{
	state_lock();
	int32_t local = local_occupancy;
	uint8_t inside = inside_count;
	bool schedule_open = access_window_open_locked();
	bool override_active = override_active_locked();
	uint16_t start = config.schedule_start_minute;
	uint16_t end = config.schedule_end_minute;
	bool lockdown = lockdown_active;
	bool emergency = emergency_active;
	uint32_t grants = granted_total;
	uint32_t denials = denied_total;
	char event_copy[64];
	snprintf(event_copy, sizeof(event_copy), "%s", last_event);
	bool event_ok = last_event_ok;
	state_unlock();

	memset(state, 0, sizeof(*state));
	state->room_name = active_profile->name;
	state->room_id = active_profile->room_id;
	state->exit_node = active_profile->exit_node;
	state->entrance = active_profile->room_id == 0;
	state->now = time(NULL);
	state->clock_set = state->now >= CLOCK_SENTINEL;
	state->local_occupancy = local;
	state->global_occupancy =
		communication_get_global_occupancy(active_profile->room_id, local);
	state->inside_count = inside;
	state->schedule_start_minute = start;
	state->schedule_end_minute = end;
	state->schedule_open = schedule_open;
	state->override_active = override_active;
	state->lockdown = lockdown;
	state->emergency = emergency;
	state->light_on = gpio_get_level(SMART_SYSTEM_PIN_LIGHT_RELAY) != 0;
	state->motion = motion_hold_active;
	state->grants = grants;
	state->denials = denials;
	snprintf(state->last_event, sizeof(state->last_event), "%s", event_copy);
	state->last_event_ok = event_ok;
}

static void build_ui_status(ui_status_t *status)
{
	display_state_t display_state;
	build_display_state(&display_state);
	memset(status, 0, sizeof(*status));
	status->room_name = active_profile->name;
	status->room_id = active_profile->room_id;
	status->exit_node = active_profile->exit_node;
	status->entrance = active_profile->room_id == 0;
	status->local_occupancy = display_state.local_occupancy;
	status->global_occupancy = display_state.global_occupancy;
	status->inside_count = display_state.inside_count;
	status->schedule_start_minute = display_state.schedule_start_minute;
	status->schedule_end_minute = display_state.schedule_end_minute;
	status->schedule_open = display_state.schedule_open;
	status->override_active = display_state.override_active;
	status->lockdown = display_state.lockdown;
	status->emergency = display_state.emergency;
	status->light_on = display_state.light_on;
	status->motion = display_state.motion;
	status->grants = display_state.grants;
	status->denials = display_state.denials;
	status->clock_set = display_state.clock_set;
	status->now = display_state.now;
	status->web_active = webui_active();
	status->log_bytes = storage_log_size();
	status->uptime_seconds = uptime_seconds();
	state_lock();
	status->allowed_count = config.allowed_count;
	status->entries = entry_total;
	status->exits = exit_total;
	snprintf(status->last_event, sizeof(status->last_event), "%s", last_event);
	status->last_event_ok = last_event_ok;
	status->last_event_time = last_event_time;
	state_unlock();
}

static void build_web_status(web_status_t *status)
{
	ui_status_t snapshot;
	build_ui_status(&snapshot);
	memset(status, 0, sizeof(*status));
	snprintf(status->room_name, sizeof(status->room_name), "%s", snapshot.room_name);
	status->room_id = snapshot.room_id;
	status->entrance = snapshot.entrance;
	status->clock_set = snapshot.clock_set;
	status->now = snapshot.now;
	status->local_occupancy = snapshot.local_occupancy;
	status->global_occupancy = snapshot.global_occupancy;
	status->inside_count = snapshot.inside_count;
	status->allowed_count = snapshot.allowed_count;
	status->schedule_start_minute = snapshot.schedule_start_minute;
	status->schedule_end_minute = snapshot.schedule_end_minute;
	status->schedule_open = snapshot.schedule_open;
	status->override_active = snapshot.override_active;
	status->lockdown = snapshot.lockdown;
	status->emergency = snapshot.emergency;
	status->light_on = snapshot.light_on;
	status->motion = snapshot.motion;
	status->grants = snapshot.grants;
	status->denials = snapshot.denials;
	snprintf(status->last_event, sizeof(status->last_event), "%s", snapshot.last_event);
	status->last_event_ok = snapshot.last_event_ok;
	status->last_event_time = snapshot.last_event_time;
}

static bool parse_uid(const char *text, uint8_t uid[4])
{
	if (text == NULL || strlen(text) != 8) {
		return false;
	}
	for (size_t index = 0; index < 4; ++index) {
		if (!isxdigit((unsigned char)text[index * 2]) ||
			!isxdigit((unsigned char)text[index * 2 + 1])) {
			return false;
		}
		char byte_text[3] = {text[index * 2], text[index * 2 + 1], '\0'};
		uid[index] = (uint8_t)strtoul(byte_text, NULL, 16);
	}
	return true;
}

static bool parse_clock(const char *text, uint16_t *minute)
{
	int hour;
	int minutes;
	char trailing;
	if (text == NULL ||
		sscanf(text, "%d:%d%c", &hour, &minutes, &trailing) != 2 || hour < 0 ||
		hour > 23 || minutes < 0 || minutes > 59) {
		return false;
	}
	*minute = (uint16_t)(hour * 60 + minutes);
	return true;
}

static void print_recent_events(size_t count)
{
	static char logs[8192];
	size_t length = storage_read_logs(logs, sizeof(logs));
	if (length == 0) {
		ui_info("No access records have been written yet.");
		return;
	}
	size_t line_count = 1;
	for (size_t index = 0; index < length; ++index) {
		if (logs[index] == '\n') {
			++line_count;
		}
	}
	size_t skip = line_count > count + 1 ? line_count - count - 1 : 0;
	size_t current = 0;
	char *cursor = logs;
	while (cursor != NULL && *cursor != '\0') {
		char *next = strchr(cursor, '\n');
		if (next != NULL) {
			*next = '\0';
		}
		if (current >= skip) {
			printf("%s\n", cursor);
		}
		++current;
		cursor = next != NULL ? next + 1 : NULL;
	}
}

static void print_uptime(void)
{
	uint32_t seconds = uptime_seconds();
	printf("Uptime: %02u:%02u:%02u\n", (unsigned)(seconds / 3600),
		   (unsigned)((seconds % 3600) / 60), (unsigned)(seconds % 60));
}

static void print_inside(void)
{
	static inside_record_t copy[MAX_INSIDE_RECORDS];
	uint8_t count = 0;
	state_lock();
	memcpy(copy, inside_records, sizeof(copy));
	count = inside_count;
	state_unlock();
	ui_print_who(copy, count);
}

static void print_rooms(void)
{
	remote_node_info_t nodes[16];
	size_t count = communication_get_remote_nodes(nodes, 16);
	ui_print_rooms(nodes, count);
}

static void console_task(void *argument)
{
	char line[160];
	bool authenticated = false;
	unsigned failed_logins = 0;
	TickType_t login_lock_until = 0;
	bool prompt_visible = false;
	(void)argument;

	while (true) {
		if (!prompt_visible) {
			if (ui_color_enabled()) {
				printf("\033[36mSmart>\033[0m ");
			} else {
				printf("Smart> ");
			}
			fflush(stdout);
			prompt_visible = true;
		}
		if (fgets(line, sizeof(line), stdin) == NULL) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		line[strcspn(line, "\r\n")] = '\0';
		char *command = strtok(line, " \t");
		char *argument_one = strtok(NULL, " \t");
		char *argument_two = strtok(NULL, " \t");
		char *argument_three = strtok(NULL, " \t");
		if (command == NULL) {
			continue;
		}
		prompt_visible = false;

		if (strcmp(command, "help") == 0) {
			ui_help();
		} else if (strcmp(command, "menu") == 0) {
			ui_menu();
		} else if (strcmp(command, "clear") == 0) {
			ui_clear_screen();
		} else if (strcmp(command, "status") == 0) {
			ui_status_t status;
			build_ui_status(&status);
			status.authenticated = authenticated;
			ui_print_status(&status);
		} else if (strcmp(command, "who") == 0) {
			print_inside();
		} else if (strcmp(command, "rooms") == 0) {
			print_rooms();
		} else if (strcmp(command, "events") == 0) {
			size_t count = 15;
			if (argument_one != NULL) {
				long parsed = strtol(argument_one, NULL, 10);
				if (parsed > 0 && parsed <= 200) {
					count = (size_t)parsed;
				}
			}
			print_recent_events(count);
		} else if (strcmp(command, "logs") == 0) {
			storage_print_logs();
		} else if (strcmp(command, "stats") == 0) {
			ui_status_t status;
			build_ui_status(&status);
			status.authenticated = authenticated;
			ui_print_stats(&status);
		} else if (strcmp(command, "uptime") == 0) {
			print_uptime();
		} else if (strcmp(command, "login") == 0) {
			bool pin_matches = false;
			if (argument_one != NULL) {
				state_lock();
				pin_matches = strcmp(argument_one, config.admin_pin) == 0;
				state_unlock();
			}
			if (tick_pending(login_lock_until)) {
				ui_warn("Login temporarily locked after repeated failures.");
			} else if (pin_matches) {
				authenticated = true;
				failed_logins = 0;
				ui_ok("Administrator session enabled.");
			} else {
				authenticated = false;
				++failed_logins;
				ui_error("Authentication failed (%u/%u).", failed_logins,
						 LOGIN_MAX_FAILURES);
				if (failed_logins >= LOGIN_MAX_FAILURES) {
					login_lock_until =
						xTaskGetTickCount() + pdMS_TO_TICKS(LOGIN_LOCK_SECONDS * 1000);
					failed_logins = 0;
					ui_warn("Too many failures. Login locked for %u seconds.",
							LOGIN_LOCK_SECONDS);
				}
			}
		} else if (strcmp(command, "logout") == 0) {
			authenticated = false;
			ui_info("Administrator session ended.");
		} else if (strcmp(command, "color") == 0) {
			if (argument_one != NULL &&
				(strcmp(argument_one, "off") == 0 || strcmp(argument_one, "on") == 0)) {
				ui_set_color(strcmp(argument_one, "on") == 0);
				ui_ok("Colour output %s.", argument_one);
			} else {
				ui_warn("Use: color on|off");
			}
		} else if (strcmp(command, "allow") == 0 || strcmp(command, "deny") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			uint8_t uid[4];
			if (!parse_uid(argument_one, uid)) {
				ui_error("Use an 8-digit hexadecimal UID.");
				continue;
			}
			state_lock();
			int found = -1;
			for (uint8_t index = 0; index < config.allowed_count; ++index) {
				if (memcmp(uid, config.allowed_uids[index], 4) == 0) {
					found = index;
					break;
				}
			}
			if (strcmp(command, "allow") == 0 && found < 0 &&
				config.allowed_count < MAX_ALLOWED_CARDS) {
				memcpy(config.allowed_uids[config.allowed_count++], uid, 4);
			} else if (strcmp(command, "deny") == 0 && found >= 0) {
				--config.allowed_count;
				memcpy(config.allowed_uids[found],
					   config.allowed_uids[config.allowed_count], 4);
				memset(config.allowed_uids[config.allowed_count], 0, 4);
			}
			uint8_t total = config.allowed_count;
			state_unlock();
			storage_save_config(&config);
			ui_ok("Allowlist updated (%u cards).", (unsigned)total);
		} else if (strcmp(command, "enroll") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			long seconds = 20;
			if (argument_one != NULL) {
				long parsed = strtol(argument_one, NULL, 10);
				if (parsed >= 5 && parsed <= 120) {
					seconds = parsed;
				}
			}
			state_lock();
			enroll_mode = true;
			enroll_until = xTaskGetTickCount() + pdMS_TO_TICKS(seconds * 1000);
			state_unlock();
			ui_info("Card enrolment armed for %ld seconds. Tap a card now.", seconds);
		} else if (strcmp(command, "schedule") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			uint16_t start;
			uint16_t end;
			if (!parse_clock(argument_one, &start) || !parse_clock(argument_two, &end) ||
				start >= end) {
				ui_error("Schedule must be a same-day range, e.g. 07:00 22:00.");
				continue;
			}
			state_lock();
			config.schedule_start_minute = start;
			config.schedule_end_minute = end;
			state_unlock();
			storage_save_config(&config);
			ui_ok("Daily access window set to %02u:%02u-%02u:%02u.", start / 60, start % 60,
				  end / 60, end % 60);
		} else if (strcmp(command, "time") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			int year, month, day, hour, minute, second;
			char trailing;
			if (argument_one == NULL || argument_two == NULL || argument_three != NULL ||
				sscanf(argument_one, "%d-%d-%d%c", &year, &month, &day, &trailing) != 3 ||
				sscanf(argument_two, "%d:%d:%d%c", &hour, &minute, &second, &trailing) !=
					3 ||
				year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
				hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
				second > 59) {
				ui_error("Use time YYYY-MM-DD HH:MM:SS.");
				continue;
			}
			struct tm set_time = {.tm_year = year - 1900, .tm_mon = month - 1,
								  .tm_mday = day, .tm_hour = hour, .tm_min = minute,
								  .tm_sec = second};
			time_t epoch = mktime(&set_time);
			struct timeval value = {.tv_sec = epoch, .tv_usec = 0};
			settimeofday(&value, NULL);
			ui_ok("System clock updated (East Africa Time).");
		} else if (strcmp(command, "pin") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			if (argument_one == NULL || strlen(argument_one) < 4 ||
				strlen(argument_one) > 8 || argument_two != NULL) {
				ui_error("PIN must be 4 to 8 digits.");
				continue;
			}
			bool digits_only = true;
			for (size_t index = 0; argument_one[index] != '\0'; ++index) {
				if (!isdigit((unsigned char)argument_one[index])) {
					digits_only = false;
					break;
				}
			}
			if (!digits_only) {
				ui_error("PIN must contain digits only.");
				continue;
			}
			state_lock();
			strcpy(config.admin_pin, argument_one);
			state_unlock();
			storage_save_config(&config);
			ui_ok("Administrator PIN changed.");
		} else if (strcmp(command, "lockdown") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			if (argument_one == NULL ||
				(strcmp(argument_one, "on") != 0 && strcmp(argument_one, "off") != 0)) {
				ui_warn("Use: lockdown on|off");
				continue;
			}
			state_lock();
			lockdown_active = strcmp(argument_one, "on") == 0;
			bool state = lockdown_active;
			set_last_event_locked(state ? "LOCKDOWN ENABLED" : "LOCKDOWN CLEARED", true);
			state_unlock();
			storage_write_event(active_profile, "SECURITY", NULL,
								state ? "lockdown-enabled" : "lockdown-disabled");
			ui_ok("Lockdown %s.", state ? "enabled" : "disabled");
		} else if (strcmp(command, "emergency") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			if (argument_one == NULL ||
				(strcmp(argument_one, "on") != 0 && strcmp(argument_one, "off") != 0)) {
				ui_warn("Use: emergency on|off");
				continue;
			}
			state_lock();
			emergency_active = strcmp(argument_one, "on") == 0;
			bool state = emergency_active;
			set_last_event_locked(state ? "EMERGENCY RELEASE" : "EMERGENCY CLEARED",
								  true);
			state_unlock();
			storage_write_event(active_profile, "SECURITY", NULL,
								state ? "emergency-release" : "emergency-cleared");
			ui_ok("Emergency release %s.", state ? "enabled" : "disabled");
		} else if (strcmp(command, "override") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			long minutes = argument_one != NULL ? strtol(argument_one, NULL, 10) : -1;
			if (minutes <= 0 || minutes > 480) {
				ui_warn("Use: override <minutes>, 1 to 480.");
				continue;
			}
			state_lock();
			override_until = xTaskGetTickCount() + pdMS_TO_TICKS(minutes * 60000);
			set_last_event_locked("SCHEDULE OVERRIDE", true);
			state_unlock();
			storage_write_event(active_profile, "SECURITY", NULL,
								"temporary-schedule-override");
			ui_ok("Access window opened for %ld minutes.", minutes);
		} else if (strcmp(command, "export") == 0) {
			ui_info("CSV access history follows.");
			storage_print_logs();
		} else if (strcmp(command, "wipe") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			storage_clear_logs();
			ui_warn("All access records have been deleted.");
		} else if (strcmp(command, "reboot") == 0) {
			if (!authenticated) {
				ui_error("Log in as administrator first.");
				continue;
			}
			ui_info("Restarting terminal...");
			vTaskDelay(pdMS_TO_TICKS(200));
			esp_restart();
		} else {
			ui_error("Unknown command. Type help.");
		}
	}
}

static void spi_bus_setup(void)
{
	spi_bus_config_t bus_config = {
		.sclk_io_num = SMART_SYSTEM_PIN_SCK,
		.mosi_io_num = SMART_SYSTEM_PIN_MOSI,
		.miso_io_num = SMART_SYSTEM_PIN_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4096,
	};
	ESP_ERROR_CHECK(
		spi_bus_initialize(SMART_SYSTEM_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
}

void app_main(void)
{
	setenv("TZ", "EAT-3", 1);
	tzset();
	spi_bus_setup();

	state_mutex = xSemaphoreCreateMutex();
	configASSERT(state_mutex != NULL);

	storage_init(SMART_SYSTEM_SPI_HOST);
	storage_load_config(&config);
	storage_load_occupancy(&local_occupancy);
	storage_load_inside(inside_records, &inside_count);

	if (active_profile->room_id == 0) {
		uint8_t valid = 0;
		for (uint8_t index = 0; index < MAX_INSIDE_RECORDS; ++index) {
			if (inside_records[index].valid) {
				++valid;
			}
		}
		inside_count = valid;
		local_occupancy = inside_count;
	}
	if (local_occupancy < 0) {
		local_occupancy = 0;
	}

	gpio_config_t output_gpio = {
		.pin_bit_mask = (1ULL << SMART_SYSTEM_PIN_DOOR_ENTRY_RELAY) |
						(1ULL << SMART_SYSTEM_PIN_DOOR_EXIT_RELAY) |
						(1ULL << SMART_SYSTEM_PIN_LIGHT_RELAY),
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&output_gpio));
	gpio_set_level(SMART_SYSTEM_PIN_DOOR_ENTRY_RELAY, 0);
	gpio_set_level(SMART_SYSTEM_PIN_DOOR_EXIT_RELAY, 0);
	gpio_set_level(SMART_SYSTEM_PIN_LIGHT_RELAY, 0);

	if (active_profile->room_id != 0) {
		gpio_config_t presence_gpio = {
			.pin_bit_mask = 1ULL << SMART_SYSTEM_PIN_PIR,
			.mode = GPIO_MODE_INPUT,
			.pull_down_en = GPIO_PULLDOWN_ENABLE,
		};
		ESP_ERROR_CHECK(gpio_config(&presence_gpio));
	}

	rc522_init(&entry_reader, SMART_SYSTEM_PIN_RFID_ENTRY_CS,
			   SMART_SYSTEM_PIN_RFID_ENTRY_RST,
			   active_profile->room_id == 0 ? false : active_profile->exit_node, true);
	if (active_profile->room_id == 0) {
		exit_reader_present =
			rc522_init(&exit_reader, SMART_SYSTEM_PIN_RFID_EXIT_CS,
					   SMART_SYSTEM_PIN_RFID_EXIT_RST, true, false);
	}

#if CONFIG_SMART_SYSTEM_ENABLE_OLED
	display_init();
#endif
	ESP_ERROR_CHECK(communication_init(active_profile->room_id));
#if CONFIG_SMART_SYSTEM_ENABLE_WEB
	webui_start(active_profile->room_id);
#endif

	boot_tick = xTaskGetTickCount();
	ESP_ERROR_CHECK(xTaskCreate(console_task, "serial_console", 8192, NULL, 5, NULL) ==
							pdPASS
						? ESP_OK
						: ESP_FAIL);

	ui_banner(active_profile->name, active_profile->room_id,
			  active_profile->room_id == 0, active_profile->exit_node);
	ui_help();
	ui_info("Terminal ready. Default administrator PIN is 1234 - change it now.");

	TickType_t last_broadcast = 0;
	TickType_t last_web_update = 0;
	while (true) {
		poll_reader(&entry_reader);
		if (exit_reader_present) {
			poll_reader(&exit_reader);
		}

		update_presence();
		update_lighting();

		state_lock();
		if (enroll_mode && !tick_pending(enroll_until)) {
			enroll_mode = false;
			ui_warn("Card enrolment timed out.");
		}
		state_unlock();

		TickType_t now = xTaskGetTickCount();
		if ((TickType_t)(now - last_broadcast) >= pdMS_TO_TICKS(1000)) {
			state_lock();
			int32_t contribution = local_occupancy;
			state_unlock();
			communication_broadcast_occupancy(active_profile->room_id, contribution);
			last_broadcast = now;
		}
		if ((TickType_t)(now - last_web_update) >= pdMS_TO_TICKS(1000)) {
			web_status_t web_status;
			build_web_status(&web_status);
			webui_update_status(&web_status);
			last_web_update = now;
		}

		display_state_t display_state;
		build_display_state(&display_state);
		display_tick(&display_state);

		vTaskDelay(pdMS_TO_TICKS(40));
	}
}
