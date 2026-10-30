#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "storage.h"

/* Defaults target a classic ESP32 DevKit and an MFRC522 SPI reader. */
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
#define RFID_SPI_CLOCK_HZ 8000000
#define PIR_LIGHT_HOLD_MS 30000
#define RFID_RESET_GPIO GPIO_NUM_26
#define ROOM1_LIGHT_GPIO GPIO_NUM_27
#define ROOM2_LIGHT_GPIO GPIO_NUM_14
#define ROOM1_PIR_GPIO GPIO_NUM_34
#define ROOM2_PIR_GPIO GPIO_NUM_35
#define ROOM1_DOOR_GPIO GPIO_NUM_32
#define ROOM2_DOOR_GPIO GPIO_NUM_33
#define ENTRANCE_DOOR_GPIO GPIO_NUM_25

static const char *TAG = "smart_system";

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
	room_profile_t profile;
	gpio_num_t chip_select_gpio;
	spi_device_handle_t spi;
} access_terminal_t;

static access_terminal_t terminals[] = {
	{{1, "Lecture Room 1 Entry", false}, GPIO_NUM_5, NULL},
	{{1, "Lecture Room 1 Exit", true}, GPIO_NUM_16, NULL},
	{{2, "Lecture Room 2 Entry", false}, GPIO_NUM_17, NULL},
	{{2, "Lecture Room 2 Exit", true}, GPIO_NUM_21, NULL},
	{{0, "Main Entrance Entry", false}, GPIO_NUM_22, NULL},
	{{0, "Main Entrance Exit", true}, GPIO_NUM_13, NULL},
};

#define TERMINAL_COUNT (sizeof(terminals) / sizeof(terminals[0]))

static const gpio_num_t door_gpios[] = {
	ROOM1_DOOR_GPIO, ROOM2_DOOR_GPIO, ENTRANCE_DOOR_GPIO
};
static const gpio_num_t light_gpios[] = {ROOM1_LIGHT_GPIO, ROOM2_LIGHT_GPIO};
static const gpio_num_t pir_gpios[] = {ROOM1_PIR_GPIO, ROOM2_PIR_GPIO};
static system_config_t config;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t config_mutex;
static int32_t occupancy_by_zone[3];
static TickType_t last_motion_tick[2];
static bool motion_hold_active[2];
static TickType_t door_release_tick[3];

static esp_err_t rc522_write(spi_device_handle_t spi, uint8_t reg, uint8_t value)
{
	uint8_t tx[2] = {(uint8_t)((reg << 1) & 0x7E), value};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
	};
	return spi_device_transmit(spi, &transaction);
}

static esp_err_t rc522_read(spi_device_handle_t spi, uint8_t reg, uint8_t *value)
{
	uint8_t tx[2] = {(uint8_t)(((reg << 1) & 0x7E) | 0x80), 0};
	uint8_t rx[2] = {0};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
		.rx_buffer = rx,
	};
	esp_err_t result = spi_device_transmit(spi, &transaction);
	if (result == ESP_OK) {
		*value = rx[1];
	}
	return result;
}

static bool rc522_transceive(spi_device_handle_t spi, const uint8_t *send_data, size_t send_length,
							 uint8_t *receive_data, size_t *receive_length,
							 uint8_t *last_bits)
{
	uint8_t value;
	if (rc522_write(spi, RC522_COM_IRQ_REG, 0x7F) != ESP_OK ||
		rc522_write(spi, RC522_FIFO_LEVEL_REG, 0x80) != ESP_OK ||
		rc522_write(spi, RC522_COMMAND_REG, 0x00) != ESP_OK) {
		return false;
	}
	for (size_t index = 0; index < send_length; ++index) {
		if (rc522_write(spi, RC522_FIFO_DATA_REG, send_data[index]) != ESP_OK) {
			return false;
		}
	}
	if (rc522_write(spi, RC522_COMMAND_REG, RC522_TRANSCEIVE) != ESP_OK ||
		rc522_write(spi, RC522_BIT_FRAMING_REG, 0x80) != ESP_OK) {
		return false;
	}

	bool completed = false;
	for (unsigned attempt = 0; attempt < 30; ++attempt) {
		vTaskDelay(pdMS_TO_TICKS(1));
		if (rc522_read(spi, RC522_COM_IRQ_REG, &value) != ESP_OK) {
			return false;
		}
		if ((value & 0x30) != 0) {
			completed = true;
			break;
		}
		if ((value & 0x01) != 0) {
			break;
		}
	}
	rc522_write(spi, RC522_BIT_FRAMING_REG, 0x00);
	if (!completed || rc522_read(spi, RC522_ERROR_REG, &value) != ESP_OK ||
		(value & 0x1B) != 0 ||
		rc522_read(spi, RC522_FIFO_LEVEL_REG, &value) != ESP_OK || value == 0) {
		return false;
	}

	size_t available = value;
	if (available > *receive_length) {
		return false;
	}
	for (size_t index = 0; index < available; ++index) {
		if (rc522_read(spi, RC522_FIFO_DATA_REG, &receive_data[index]) != ESP_OK) {
			return false;
		}
	}
	*receive_length = available;
	if (last_bits != NULL && rc522_read(spi, RC522_CONTROL_REG, &value) == ESP_OK) {
		*last_bits = value & 0x07;
	}
	return true;
}

static bool rc522_read_uid(spi_device_handle_t spi, uint8_t uid[4])
{
	const uint8_t request[] = {0x26};
	uint8_t response[5];
	size_t response_length = sizeof(response);
	uint8_t last_bits = 0;
	if (rc522_write(spi, RC522_BIT_FRAMING_REG, 0x07) != ESP_OK ||
		!rc522_transceive(spi, request, sizeof(request), response, &response_length,
						  &last_bits) || response_length != 2 || last_bits != 0) {
		return false;
	}

	const uint8_t anticollision[] = {0x93, 0x20};
	response_length = sizeof(response);
	if (!rc522_transceive(spi, anticollision, sizeof(anticollision), response,
						  &response_length, NULL) || response_length != 5 ||
		(uint8_t)(response[0] ^ response[1] ^ response[2] ^ response[3]) != response[4]) {
		return false;
	}
	memcpy(uid, response, 4);
	return true;
}

static void rc522_init(access_terminal_t *terminal)
{
	spi_device_handle_t spi = terminal->spi;
	ESP_ERROR_CHECK(rc522_write(spi, RC522_COMMAND_REG, RC522_SOFT_RESET));
	vTaskDelay(pdMS_TO_TICKS(50));
	ESP_ERROR_CHECK(rc522_write(spi, 0x2A, 0x8D));
	ESP_ERROR_CHECK(rc522_write(spi, 0x2B, 0x3E));
	ESP_ERROR_CHECK(rc522_write(spi, 0x2D, 30));
	ESP_ERROR_CHECK(rc522_write(spi, 0x2C, 0));
	ESP_ERROR_CHECK(rc522_write(spi, RC522_TX_ASK_REG, 0x40));
	ESP_ERROR_CHECK(rc522_write(spi, RC522_MODE_REG, 0x3D));
	uint8_t tx_control;
	ESP_ERROR_CHECK(rc522_read(spi, RC522_TX_CONTROL_REG, &tx_control));
	if ((tx_control & 0x03) != 0x03) {
		ESP_ERROR_CHECK(rc522_write(spi, RC522_TX_CONTROL_REG, tx_control | 0x03));
	}
	uint8_t version;
	ESP_ERROR_CHECK(rc522_read(spi, RC522_VERSION_REG, &version));
	if (version == 0x00 || version == 0xFF) {
		ESP_LOGW(TAG,
				 "%s RFID returned 0x%02X; check shared SPI, CS GPIO %d, and reset GPIO %d",
				 terminal->profile.name, version, terminal->chip_select_gpio,
				 RFID_RESET_GPIO);
	} else {
		ESP_LOGI(TAG, "%s RFID version register: 0x%02X",
				 terminal->profile.name, version);
	}
}

static bool access_window_open(void)
{
	time_t now = time(NULL);
	if (now < 1760000000) {
		return false;
	}
	struct tm local_time;
	if (localtime_r(&now, &local_time) == NULL) {
		return false;
	}
	uint16_t minute = (uint16_t)(local_time.tm_hour * 60 + local_time.tm_min);
	xSemaphoreTake(config_mutex, portMAX_DELAY);
	uint16_t schedule_start = config.schedule_start_minute;
	uint16_t schedule_end = config.schedule_end_minute;
	xSemaphoreGive(config_mutex);
	return minute >= schedule_start && minute < schedule_end;
}

static bool uid_is_allowed(const uint8_t uid[4])
{
	bool allowed = false;
	xSemaphoreTake(config_mutex, portMAX_DELAY);
	for (uint8_t index = 0; index < config.allowed_count; ++index) {
		if (memcmp(uid, config.allowed_uids[index], 4) == 0) {
			allowed = true;
			break;
		}
	}
	xSemaphoreGive(config_mutex);
	return allowed;
}

static int32_t get_zone_occupancy(uint8_t room_id)
{
	if (room_id > 2) {
		return 0;
	}
	int32_t occupancy;
	portENTER_CRITICAL(&state_lock);
	occupancy = occupancy_by_zone[room_id];
	portEXIT_CRITICAL(&state_lock);
	return occupancy > 0 ? occupancy : 0;
}

static void update_lighting(void)
{
	TickType_t now = xTaskGetTickCount();
	for (size_t room = 0; room < 2; ++room) {
		bool motion_detected = gpio_get_level(pir_gpios[room]) != 0;
		if (motion_detected) {
			last_motion_tick[room] = now;
			motion_hold_active[room] = true;
		} else if (motion_hold_active[room] &&
				   (TickType_t)(now - last_motion_tick[room]) >=
					   pdMS_TO_TICKS(PIR_LIGHT_HOLD_MS)) {
			motion_hold_active[room] = false;
		}
		gpio_set_level(light_gpios[room], motion_hold_active[room] ? 1 : 0);
	}
}

static void handle_card(const access_terminal_t *terminal, const uint8_t uid[4])
{
	bool allowed = uid_is_allowed(uid);
	if (!allowed) {
		storage_write_event(&terminal->profile, "DENIED", uid, "unknown-card");
		return;
	}
	if (!terminal->profile.exit_node && !access_window_open()) {
		storage_write_event(&terminal->profile, "DENIED", uid,
						"outside-schedule-or-clock-unset");
		return;
	}

	gpio_num_t door_gpio = door_gpios[terminal->profile.room_id];
	gpio_set_level(door_gpio, 1);
	door_release_tick[terminal->profile.room_id] =
		xTaskGetTickCount() + pdMS_TO_TICKS(700);

	portENTER_CRITICAL(&state_lock);
	if (terminal->profile.exit_node) {
		if (occupancy_by_zone[terminal->profile.room_id] > 0) {
			--occupancy_by_zone[terminal->profile.room_id];
		}
	} else {
		++occupancy_by_zone[terminal->profile.room_id];
	}
	int32_t occupancy = occupancy_by_zone[terminal->profile.room_id];
	portEXIT_CRITICAL(&state_lock);
	storage_save_occupancy(terminal->profile.room_id, occupancy);
	update_lighting();
	storage_write_event(&terminal->profile,
					terminal->profile.exit_node ? "EXIT" : "ENTRY", uid,
					terminal->profile.exit_node ? "occupancy-decremented" :
												"occupancy-incremented");
	ESP_LOGI(TAG, "%s accepted; estimated occupancy for zone %u: %" PRId32,
			 terminal->profile.name, (unsigned)terminal->profile.room_id, occupancy);
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
	if (sscanf(text, "%d:%d%c", &hour, &minutes, &trailing) != 2 ||
		hour < 0 || hour > 23 || minutes < 0 || minutes > 59) {
		return false;
	}
	*minute = (uint16_t)(hour * 60 + minutes);
	return true;
}

static void print_help(void)
{
	printf("Commands: help, status, login <PIN>, logout, allow <8-hex-UID>, "
		   "deny <8-hex-UID>, schedule <HH:MM> <HH:MM>, "
		   "time <YYYY-MM-DD> <HH:MM:SS>, pin <new-PIN>, logs\n");
}

static void print_status(void)
{
	uint8_t allowed_count;
	uint16_t schedule_start;
	uint16_t schedule_end;
	xSemaphoreTake(config_mutex, portMAX_DELAY);
	allowed_count = config.allowed_count;
	schedule_start = config.schedule_start_minute;
	schedule_end = config.schedule_end_minute;
	xSemaphoreGive(config_mutex);
	printf("Central controller | estimated occupancy: Room 1=%" PRId32
		   ", Room 2=%" PRId32 ", entrance=%" PRId32 "\n",
		   get_zone_occupancy(1), get_zone_occupancy(2), get_zone_occupancy(0));
	printf("Allowed cards: %u | schedule: %02u:%02u-%02u:%02u | clock: ",
		   allowed_count, schedule_start / 60, schedule_start % 60,
		   schedule_end / 60, schedule_end % 60);
	time_t now = time(NULL);
	if (now < 1760000000) {
		printf("not set\n");
	} else {
		struct tm local_time;
		char timestamp[32];
		localtime_r(&now, &local_time);
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
		printf("%s\n", timestamp);
	}
	printf("Room 1 light: %s | PIR: %s\n",
		   gpio_get_level(ROOM1_LIGHT_GPIO) ? "ON" : "OFF",
		   gpio_get_level(ROOM1_PIR_GPIO) ? "motion" : "clear");
	printf("Room 2 light: %s | PIR: %s\n",
		   gpio_get_level(ROOM2_LIGHT_GPIO) ? "ON" : "OFF",
		   gpio_get_level(ROOM2_PIR_GPIO) ? "motion" : "clear");
}

static void print_logs(void)
{
	storage_print_logs();
}

static void console_task(void *argument)
{
	char line[128];
	bool authenticated = false;
	bool prompt_pending = true;
	(void)argument;
	print_help();
	while (true) {
		if (prompt_pending) {
			printf("smart> ");
			fflush(stdout);
			prompt_pending = false;
		}
		if (fgets(line, sizeof(line), stdin) == NULL) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		prompt_pending = true;
		line[strcspn(line, "\r\n")] = '\0';
		char *command = strtok(line, " \t");
		char *argument_one = strtok(NULL, " \t");
		char *argument_two = strtok(NULL, " \t");
		char *argument_three = strtok(NULL, " \t");
		if (command == NULL) {
			continue;
		}
		if (strcmp(command, "help") == 0) {
			print_help();
		} else if (strcmp(command, "status") == 0) {
			print_status();
		} else if (strcmp(command, "logs") == 0) {
			print_logs();
		} else if (strcmp(command, "login") == 0) {
			bool pin_matches = false;
			if (argument_one != NULL) {
				xSemaphoreTake(config_mutex, portMAX_DELAY);
				pin_matches = strcmp(argument_one, config.admin_pin) == 0;
				xSemaphoreGive(config_mutex);
			}
			if (pin_matches) {
				authenticated = true;
				printf("Administrator session enabled.\n");
			} else {
				authenticated = false;
				printf("Authentication failed.\n");
			}
		} else if (strcmp(command, "logout") == 0) {
			authenticated = false;
			printf("Administrator session ended.\n");
		} else if (strcmp(command, "allow") == 0 || strcmp(command, "deny") == 0 ||
				   strcmp(command, "schedule") == 0 || strcmp(command, "time") == 0 ||
				   strcmp(command, "pin") == 0) {
			if (!authenticated) {
				printf("Log in as administrator first.\n");
				continue;
			}
			if (strcmp(command, "allow") == 0 || strcmp(command, "deny") == 0) {
				uint8_t uid[4];
				if (!parse_uid(argument_one, uid)) {
					printf("Use an 8-digit hexadecimal UID.\n");
					continue;
				}
				xSemaphoreTake(config_mutex, portMAX_DELAY);
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
				storage_save_config(&config);
				xSemaphoreGive(config_mutex);
				printf("Allowlist updated (%u cards).\n", config.allowed_count);
			} else if (strcmp(command, "schedule") == 0) {
				uint16_t start;
				uint16_t end;
				if (!parse_clock(argument_one, &start) ||
					!parse_clock(argument_two, &end) || start >= end) {
					printf("Schedule must be a same-day range, e.g. 07:00 22:00.\n");
					continue;
				}
				xSemaphoreTake(config_mutex, portMAX_DELAY);
				config.schedule_start_minute = start;
				config.schedule_end_minute = end;
				storage_save_config(&config);
				xSemaphoreGive(config_mutex);
				printf("Schedule saved.\n");
			} else if (strcmp(command, "time") == 0) {
				int year, month, day, hour, minute, second;
				char trailing;
				if (argument_one == NULL || argument_two == NULL || argument_three != NULL ||
					sscanf(argument_one, "%d-%d-%d%c", &year, &month, &day, &trailing) != 3 ||
					sscanf(argument_two, "%d:%d:%d%c", &hour, &minute, &second, &trailing) != 3 ||
					year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
					hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
					second < 0 || second > 59) {
					printf("Use time YYYY-MM-DD HH:MM:SS.\n");
					continue;
				}
				struct tm set_time = {.tm_year = year - 1900, .tm_mon = month - 1,
									  .tm_mday = day, .tm_hour = hour,
									  .tm_min = minute, .tm_sec = second};
				time_t epoch = mktime(&set_time);
				struct timeval value = {.tv_sec = epoch, .tv_usec = 0};
				settimeofday(&value, NULL);
				printf("System clock set (configure TZ in firmware for local time).\n");
			} else if (strcmp(command, "pin") == 0) {
				if (argument_one == NULL || strlen(argument_one) < 4 ||
					strlen(argument_one) > 8 || argument_two != NULL) {
					printf("PIN must be 4 to 8 characters.\n");
					continue;
				}
				for (size_t index = 0; argument_one[index] != '\0'; ++index) {
					if (!isdigit((unsigned char)argument_one[index])) {
						printf("PIN must contain digits only.\n");
						argument_one = NULL;
						break;
					}
				}
				if (argument_one != NULL) {
					xSemaphoreTake(config_mutex, portMAX_DELAY);
					strcpy(config.admin_pin, argument_one);
					storage_save_config(&config);
					xSemaphoreGive(config_mutex);
					printf("Administrator PIN changed.\n");
				}
			}
		} else {
			printf("Unknown command. Type help.\n");
		}
	}
}

void app_main(void)
{
	spi_bus_config_t bus_config = {
		.sclk_io_num = SMART_SYSTEM_PIN_SCK,
		.mosi_io_num = SMART_SYSTEM_PIN_MOSI,
		.miso_io_num = SMART_SYSTEM_PIN_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4000,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(SMART_SYSTEM_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
	storage_init(SMART_SYSTEM_SPI_HOST);
	storage_load_config(&config);
	config_mutex = xSemaphoreCreateMutex();
	ESP_ERROR_CHECK(config_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
	for (uint8_t zone = 0; zone < 3; ++zone) {
		storage_load_occupancy(zone, &occupancy_by_zone[zone]);
	}

	uint64_t output_mask = 1ULL << RFID_RESET_GPIO;
	for (size_t index = 0; index < 3; ++index) {
		output_mask |= 1ULL << door_gpios[index];
	}
	for (size_t index = 0; index < 2; ++index) {
		output_mask |= 1ULL << light_gpios[index];
	}
	gpio_config_t output_gpio = {
		.pin_bit_mask = output_mask,
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&output_gpio));
	for (size_t index = 0; index < 3; ++index) {
		gpio_set_level(door_gpios[index], 0);
	}
	for (size_t index = 0; index < 2; ++index) {
		gpio_set_level(light_gpios[index], 0);
	}
	gpio_set_level(RFID_RESET_GPIO, 0);
	vTaskDelay(pdMS_TO_TICKS(50));
	gpio_set_level(RFID_RESET_GPIO, 1);
	vTaskDelay(pdMS_TO_TICKS(50));

	gpio_config_t presence_gpio = {
		.pin_bit_mask = (1ULL << ROOM1_PIR_GPIO) | (1ULL << ROOM2_PIR_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&presence_gpio));

	spi_device_interface_config_t device_config = {
		.clock_speed_hz = RFID_SPI_CLOCK_HZ,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 1,
	};
	for (size_t index = 0; index < TERMINAL_COUNT; ++index) {
		device_config.spics_io_num = terminals[index].chip_select_gpio;
		ESP_ERROR_CHECK(spi_bus_add_device(SMART_SYSTEM_SPI_HOST, &device_config,
											 &terminals[index].spi));
		rc522_init(&terminals[index]);
	}

	ESP_ERROR_CHECK(xTaskCreate(console_task, "serial_console", 4096, NULL, 5, NULL) == pdPASS
						? ESP_OK
						: ESP_FAIL);
	ESP_LOGI(TAG, "Central controller ready: 6 RFID endpoints, 3 access zones, 2 room lights");

	uint8_t last_uid[TERMINAL_COUNT][4] = {{0}};
	TickType_t last_card_tick[TERMINAL_COUNT] = {0};
	while (true) {
		TickType_t now = xTaskGetTickCount();
		for (size_t index = 0; index < TERMINAL_COUNT; ++index) {
			uint8_t uid[4];
			if (rc522_read_uid(terminals[index].spi, uid)) {
				if (memcmp(uid, last_uid[index], sizeof(uid)) != 0 ||
					(TickType_t)(now - last_card_tick[index]) > pdMS_TO_TICKS(2500)) {
					memcpy(last_uid[index], uid, sizeof(uid));
					last_card_tick[index] = now;
					handle_card(&terminals[index], uid);
				}
			} else if ((TickType_t)(now - last_card_tick[index]) > pdMS_TO_TICKS(500)) {
				memset(last_uid[index], 0, sizeof(last_uid[index]));
			}
		}
		for (size_t zone = 0; zone < 3; ++zone) {
			if (door_release_tick[zone] != 0 &&
				(int32_t)(now - door_release_tick[zone]) >= 0) {
				gpio_set_level(door_gpios[zone], 0);
				door_release_tick[zone] = 0;
			}
		}
		update_lighting();
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
