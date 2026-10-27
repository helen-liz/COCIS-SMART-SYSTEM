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
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_rom_sys.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

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
#ifndef SMART_SYSTEM_PIN_RFID_CS
#define SMART_SYSTEM_PIN_RFID_CS 5
#endif
#ifndef SMART_SYSTEM_PIN_RFID_RST
#define SMART_SYSTEM_PIN_RFID_RST 22
#endif
#ifndef SMART_SYSTEM_PIN_DOOR_RELAY
#define SMART_SYSTEM_PIN_DOOR_RELAY 26
#endif
#ifndef SMART_SYSTEM_PIN_LIGHT_RELAY
#define SMART_SYSTEM_PIN_LIGHT_RELAY 27
#endif
#ifndef SMART_SYSTEM_EXIT_NODE
#define SMART_SYSTEM_EXIT_NODE 0
#endif
#ifndef SMART_SYSTEM_ROOM_ID
#define SMART_SYSTEM_ROOM_ID 1
#endif
#ifndef SMART_SYSTEM_ROOM_NAME
#define SMART_SYSTEM_ROOM_NAME "Lecture Room 1"
#endif

#define RFID_SPI_CLOCK_HZ 8000000
#define MAX_ALLOWED_CARDS 32
#define MAX_REMOTE_NODES 16
#define SYNC_PACKET_MAGIC 0x53434331u
#define ACCESS_LOG_PATH "/spiffs/events.csv"

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
	uint8_t allowed_uids[MAX_ALLOWED_CARDS][4];
	uint8_t allowed_count;
	uint16_t schedule_start_minute;
	uint16_t schedule_end_minute;
	char admin_pin[9];
} system_config_t;

typedef struct {
	uint32_t magic;
	uint8_t room_id;
	uint8_t reserved[3];
	int32_t occupancy;
} sync_packet_t;

typedef struct {
	uint8_t mac[6];
	int32_t occupancy;
	TickType_t last_seen;
	bool active;
} remote_node_t;

static spi_device_handle_t rfid_spi;
static nvs_handle_t config_nvs;
static system_config_t config;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE remote_lock = portMUX_INITIALIZER_UNLOCKED;
static remote_node_t remote_nodes[MAX_REMOTE_NODES];
static int32_t local_occupancy;
static bool spiffs_mounted;

static esp_err_t rc522_write(uint8_t reg, uint8_t value)
{
	uint8_t tx[2] = {(uint8_t)((reg << 1) & 0x7E), value};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
	};
	return spi_device_transmit(rfid_spi, &transaction);
}

static esp_err_t rc522_read(uint8_t reg, uint8_t *value)
{
	uint8_t tx[2] = {(uint8_t)(((reg << 1) & 0x7E) | 0x80), 0};
	uint8_t rx[2] = {0};
	spi_transaction_t transaction = {
		.length = sizeof(tx) * 8,
		.tx_buffer = tx,
		.rx_buffer = rx,
	};
	esp_err_t result = spi_device_transmit(rfid_spi, &transaction);
	if (result == ESP_OK) {
		*value = rx[1];
	}
	return result;
}

static bool rc522_transceive(const uint8_t *send_data, size_t send_length,
							 uint8_t *receive_data, size_t *receive_length,
							 uint8_t *last_bits)
{
	uint8_t value;
	if (rc522_write(RC522_COM_IRQ_REG, 0x7F) != ESP_OK ||
		rc522_write(RC522_FIFO_LEVEL_REG, 0x80) != ESP_OK ||
		rc522_write(RC522_COMMAND_REG, 0x00) != ESP_OK) {
		return false;
	}
	for (size_t index = 0; index < send_length; ++index) {
		if (rc522_write(RC522_FIFO_DATA_REG, send_data[index]) != ESP_OK) {
			return false;
		}
	}
	if (rc522_write(RC522_COMMAND_REG, RC522_TRANSCEIVE) != ESP_OK ||
		rc522_write(RC522_BIT_FRAMING_REG, 0x80) != ESP_OK) {
		return false;
	}

	bool completed = false;
	for (unsigned attempt = 0; attempt < 30; ++attempt) {
		vTaskDelay(pdMS_TO_TICKS(1));
		if (rc522_read(RC522_COM_IRQ_REG, &value) != ESP_OK) {
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
	rc522_write(RC522_BIT_FRAMING_REG, 0x00);
	if (!completed || rc522_read(RC522_ERROR_REG, &value) != ESP_OK ||
		(value & 0x1B) != 0 ||
		rc522_read(RC522_FIFO_LEVEL_REG, &value) != ESP_OK || value == 0) {
		return false;
	}

	size_t available = value;
	if (available > *receive_length) {
		return false;
	}
	for (size_t index = 0; index < available; ++index) {
		if (rc522_read(RC522_FIFO_DATA_REG, &receive_data[index]) != ESP_OK) {
			return false;
		}
	}
	*receive_length = available;
	if (last_bits != NULL && rc522_read(RC522_CONTROL_REG, &value) == ESP_OK) {
		*last_bits = value & 0x07;
	}
	return true;
}

static bool rc522_read_uid(uint8_t uid[4])
{
	const uint8_t request[] = {0x26};
	uint8_t response[5];
	size_t response_length = sizeof(response);
	uint8_t last_bits = 0;
	if (rc522_write(RC522_BIT_FRAMING_REG, 0x07) != ESP_OK ||
		!rc522_transceive(request, sizeof(request), response, &response_length,
						  &last_bits) || response_length != 2 || last_bits != 0) {
		return false;
	}

	const uint8_t anticollision[] = {0x93, 0x20};
	response_length = sizeof(response);
	if (!rc522_transceive(anticollision, sizeof(anticollision), response,
						  &response_length, NULL) || response_length != 5 ||
		(uint8_t)(response[0] ^ response[1] ^ response[2] ^ response[3]) != response[4]) {
		return false;
	}
	memcpy(uid, response, 4);
	return true;
}

static void rc522_init(void)
{
	gpio_config_t reset_gpio = {
		.pin_bit_mask = 1ULL << SMART_SYSTEM_PIN_RFID_RST,
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&reset_gpio));
	gpio_set_level(SMART_SYSTEM_PIN_RFID_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(20));
	gpio_set_level(SMART_SYSTEM_PIN_RFID_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(50));

	ESP_ERROR_CHECK(rc522_write(RC522_COMMAND_REG, RC522_SOFT_RESET));
	vTaskDelay(pdMS_TO_TICKS(50));
	ESP_ERROR_CHECK(rc522_write(0x2A, 0x8D));
	ESP_ERROR_CHECK(rc522_write(0x2B, 0x3E));
	ESP_ERROR_CHECK(rc522_write(0x2D, 30));
	ESP_ERROR_CHECK(rc522_write(0x2C, 0));
	ESP_ERROR_CHECK(rc522_write(RC522_TX_ASK_REG, 0x40));
	ESP_ERROR_CHECK(rc522_write(RC522_MODE_REG, 0x3D));
	uint8_t tx_control;
	ESP_ERROR_CHECK(rc522_read(RC522_TX_CONTROL_REG, &tx_control));
	if ((tx_control & 0x03) != 0x03) {
		ESP_ERROR_CHECK(rc522_write(RC522_TX_CONTROL_REG, tx_control | 0x03));
	}
	uint8_t version;
	ESP_ERROR_CHECK(rc522_read(RC522_VERSION_REG, &version));
	ESP_LOGI(TAG, "MFRC522 version register: 0x%02X", version);
}

static void save_config(void)
{
	nvs_set_blob(config_nvs, "uids", config.allowed_uids,
				 sizeof(config.allowed_uids));
	nvs_set_u8(config_nvs, "uid_count", config.allowed_count);
	nvs_set_u16(config_nvs, "start_min", config.schedule_start_minute);
	nvs_set_u16(config_nvs, "end_min", config.schedule_end_minute);
	nvs_set_str(config_nvs, "admin_pin", config.admin_pin);
	ESP_ERROR_CHECK(nvs_commit(config_nvs));
}

static void load_config(void)
{
	memset(&config, 0, sizeof(config));
	config.schedule_start_minute = 7 * 60;
	config.schedule_end_minute = 22 * 60;
	strcpy(config.admin_pin, "1234");

	size_t blob_size = sizeof(config.allowed_uids);
	nvs_get_blob(config_nvs, "uids", config.allowed_uids, &blob_size);
	nvs_get_u8(config_nvs, "uid_count", &config.allowed_count);
	nvs_get_u16(config_nvs, "start_min", &config.schedule_start_minute);
	nvs_get_u16(config_nvs, "end_min", &config.schedule_end_minute);
	size_t pin_size = sizeof(config.admin_pin);
	nvs_get_str(config_nvs, "admin_pin", config.admin_pin, &pin_size);
	if (config.allowed_count > MAX_ALLOWED_CARDS) {
		config.allowed_count = 0;
	}
	if (config.schedule_start_minute >= 1440 ||
		config.schedule_end_minute >= 1440 ||
		config.schedule_start_minute >= config.schedule_end_minute) {
		config.schedule_start_minute = 7 * 60;
		config.schedule_end_minute = 22 * 60;
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
	return minute >= config.schedule_start_minute &&
		   minute < config.schedule_end_minute;
}

static bool uid_is_allowed(const uint8_t uid[4])
{
	for (uint8_t index = 0; index < config.allowed_count; ++index) {
		if (memcmp(uid, config.allowed_uids[index], 4) == 0) {
			return true;
		}
	}
	return false;
}

static void write_event(const char *event, const uint8_t *uid, const char *detail)
{
	time_t now = time(NULL);
	struct tm event_time;
	char timestamp[24] = "time-not-set";
	if (now >= 1760000000 && localtime_r(&now, &event_time) != NULL) {
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &event_time);
	}
	char uid_text[12] = "--------";
	if (uid != NULL) {
		snprintf(uid_text, sizeof(uid_text), "%02X%02X%02X%02X",
				 uid[0], uid[1], uid[2], uid[3]);
	}
	ESP_LOGI(TAG, "room=%u (%s) %s uid=%s %s",
			 (unsigned)SMART_SYSTEM_ROOM_ID, SMART_SYSTEM_ROOM_NAME,
			 event, uid_text, detail);
	if (!spiffs_mounted) {
		return;
	}
	FILE *log_file = fopen(ACCESS_LOG_PATH, "a");
	if (log_file != NULL) {
		fprintf(log_file, "%s,%u,%s,%s,%s,%s\n", timestamp,
				(unsigned)SMART_SYSTEM_ROOM_ID, SMART_SYSTEM_ROOM_NAME,
				event, uid_text, detail);
		fclose(log_file);
	}
}

static int32_t get_global_occupancy(void)
{
	int32_t total;
	portENTER_CRITICAL(&state_lock);
	total = local_occupancy;
	portEXIT_CRITICAL(&state_lock);
	TickType_t now = xTaskGetTickCount();
	portENTER_CRITICAL(&remote_lock);
	for (size_t index = 0; index < MAX_REMOTE_NODES; ++index) {
		if (remote_nodes[index].active &&
			(TickType_t)(now - remote_nodes[index].last_seen) < pdMS_TO_TICKS(10000)) {
			total += remote_nodes[index].occupancy;
		}
	}
	portEXIT_CRITICAL(&remote_lock);
	return total > 0 ? total : 0;
}

static void update_lighting(void)
{
	gpio_set_level(SMART_SYSTEM_PIN_LIGHT_RELAY, get_global_occupancy() > 0 ? 1 : 0);
}

static void handle_card(const uint8_t uid[4])
{
	bool allowed = uid_is_allowed(uid);
	if (!allowed) {
		write_event("DENIED", uid, "unknown-card");
		return;
	}
	if (!access_window_open()) {
		write_event("DENIED", uid, "outside-schedule-or-clock-unset");
		return;
	}

	gpio_set_level(SMART_SYSTEM_PIN_DOOR_RELAY, 1);
	vTaskDelay(pdMS_TO_TICKS(700));
	gpio_set_level(SMART_SYSTEM_PIN_DOOR_RELAY, 0);

	portENTER_CRITICAL(&state_lock);
	if (SMART_SYSTEM_EXIT_NODE) {
		--local_occupancy;
	} else {
		++local_occupancy;
	}
	int32_t occupancy = local_occupancy;
	portEXIT_CRITICAL(&state_lock);
	nvs_set_i32(config_nvs, "occupancy", occupancy);
	ESP_ERROR_CHECK(nvs_commit(config_nvs));
	update_lighting();
	write_event(SMART_SYSTEM_EXIT_NODE ? "EXIT" : "ENTRY", uid,
				SMART_SYSTEM_EXIT_NODE ? "occupancy-decremented" : "occupancy-incremented");
	ESP_LOGI(TAG, "%s (room %u): local contribution %" PRId32
			 "; room occupancy %" PRId32,
			 SMART_SYSTEM_ROOM_NAME, (unsigned)SMART_SYSTEM_ROOM_ID, occupancy,
			 get_global_occupancy());
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
	printf("Room: %s (ID %u) | terminal: %s | local contribution: %" PRId32
		   " | room occupancy: %" PRId32 "\n",
		   SMART_SYSTEM_ROOM_NAME, (unsigned)SMART_SYSTEM_ROOM_ID,
		   SMART_SYSTEM_EXIT_NODE ? "exit" : "entry", local_occupancy,
		   get_global_occupancy());
	printf("Allowed cards: %u | schedule: %02u:%02u-%02u:%02u | clock: ",
		   config.allowed_count, config.schedule_start_minute / 60,
		   config.schedule_start_minute % 60, config.schedule_end_minute / 60,
		   config.schedule_end_minute % 60);
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
}

static void print_logs(void)
{
	if (!spiffs_mounted) {
		printf("Local log storage is unavailable.\n");
		return;
	}
	FILE *log_file = fopen(ACCESS_LOG_PATH, "r");
	if (log_file == NULL) {
		printf("No events recorded yet.\n");
		return;
	}
	char line[160];
	while (fgets(line, sizeof(line), log_file) != NULL) {
		fputs(line, stdout);
	}
	fclose(log_file);
}

static void console_task(void *argument)
{
	char line[128];
	bool authenticated = false;
	(void)argument;
	print_help();
	while (true) {
		printf("smart> ");
		fflush(stdout);
		if (fgets(line, sizeof(line), stdin) == NULL) {
			vTaskDelay(pdMS_TO_TICKS(100));
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
		if (strcmp(command, "help") == 0) {
			print_help();
		} else if (strcmp(command, "status") == 0) {
			print_status();
		} else if (strcmp(command, "logs") == 0) {
			print_logs();
		} else if (strcmp(command, "login") == 0) {
			if (argument_one != NULL && strcmp(argument_one, config.admin_pin) == 0) {
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
				save_config();
				printf("Allowlist updated (%u cards).\n", config.allowed_count);
			} else if (strcmp(command, "schedule") == 0) {
				uint16_t start;
				uint16_t end;
				if (!parse_clock(argument_one, &start) ||
					!parse_clock(argument_two, &end) || start >= end) {
					printf("Schedule must be a same-day range, e.g. 07:00 22:00.\n");
					continue;
				}
				config.schedule_start_minute = start;
				config.schedule_end_minute = end;
				save_config();
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
					strcpy(config.admin_pin, argument_one);
					save_config();
					printf("Administrator PIN changed.\n");
				}
			}
		} else {
			printf("Unknown command. Type help.\n");
		}
	}
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void espnow_receive(const esp_now_recv_info_t *info, const uint8_t *data,
						   int data_length)
#else
static void espnow_receive(const uint8_t *mac_address, const uint8_t *data,
						   int data_length)
#endif
{
	if (data_length != sizeof(sync_packet_t)) {
		return;
	}
	sync_packet_t packet;
	memcpy(&packet, data, sizeof(packet));
	if (packet.magic != SYNC_PACKET_MAGIC || packet.room_id != SMART_SYSTEM_ROOM_ID) {
		return;
	}
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    const uint8_t *mac_address = info->src_addr;
#endif
	portENTER_CRITICAL(&remote_lock);
	size_t slot = MAX_REMOTE_NODES;
	for (size_t index = 0; index < MAX_REMOTE_NODES; ++index) {
		if (remote_nodes[index].active &&
			memcmp(remote_nodes[index].mac, mac_address, 6) == 0) {
			slot = index;
			break;
		}
		if (!remote_nodes[index].active && slot == MAX_REMOTE_NODES) {
			slot = index;
		}
	}
	if (slot < MAX_REMOTE_NODES) {
		memcpy(remote_nodes[slot].mac, mac_address, 6);
		remote_nodes[slot].occupancy = packet.occupancy;
		remote_nodes[slot].last_seen = xTaskGetTickCount();
		remote_nodes[slot].active = true;
	}
	portEXIT_CRITICAL(&remote_lock);
}

static void initialize_espnow(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
	ESP_ERROR_CHECK(esp_now_init());
	ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_receive));

	esp_now_peer_info_t peer = {0};
	memset(peer.peer_addr, 0xFF, sizeof(peer.peer_addr));
	peer.channel = 1;
	peer.ifidx = WIFI_IF_STA;
	peer.encrypt = false;
	ESP_ERROR_CHECK(esp_now_add_peer(&peer));
	ESP_LOGI(TAG, "ESP-NOW occupancy synchronization active on channel 1");
}

static void broadcast_occupancy(void)
{
	static const uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	sync_packet_t packet = {
		.magic = SYNC_PACKET_MAGIC,
		.room_id = SMART_SYSTEM_ROOM_ID,
		.occupancy = local_occupancy,
	};
	esp_err_t result = esp_now_send(broadcast_address, (const uint8_t *)&packet,
									sizeof(packet));
	if (result != ESP_OK && result != ESP_ERR_ESPNOW_NO_MEM) {
		ESP_LOGW(TAG, "Occupancy broadcast failed: %s", esp_err_to_name(result));
	}
}

void app_main(void)
{
	esp_err_t nvs_result = nvs_flash_init();
	if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
		nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	} else {
		ESP_ERROR_CHECK(nvs_result);
	}
	ESP_ERROR_CHECK(nvs_open("smart_system", NVS_READWRITE, &config_nvs));
	load_config();
	nvs_get_i32(config_nvs, "occupancy", &local_occupancy);

	esp_vfs_spiffs_conf_t spiffs_config = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 4,
		.format_if_mount_failed = true,
	};
	spiffs_mounted = esp_vfs_spiffs_register(&spiffs_config) == ESP_OK;
	if (!spiffs_mounted) {
		ESP_LOGW(TAG, "SPIFFS unavailable; access events will be logged to serial only");
	}

	gpio_config_t output_gpio = {
		.pin_bit_mask = (1ULL << SMART_SYSTEM_PIN_DOOR_RELAY) |
						(1ULL << SMART_SYSTEM_PIN_LIGHT_RELAY),
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_ERROR_CHECK(gpio_config(&output_gpio));
	gpio_set_level(SMART_SYSTEM_PIN_DOOR_RELAY, 0);
	gpio_set_level(SMART_SYSTEM_PIN_LIGHT_RELAY, 0);

	spi_bus_config_t bus_config = {
		.sclk_io_num = SMART_SYSTEM_PIN_SCK,
		.mosi_io_num = SMART_SYSTEM_PIN_MOSI,
		.miso_io_num = SMART_SYSTEM_PIN_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 16,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(SMART_SYSTEM_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
	spi_device_interface_config_t device_config = {
		.clock_speed_hz = RFID_SPI_CLOCK_HZ,
		.mode = 0,
		.spics_io_num = SMART_SYSTEM_PIN_RFID_CS,
		.queue_size = 1,
	};
	ESP_ERROR_CHECK(spi_bus_add_device(SMART_SYSTEM_SPI_HOST, &device_config, &rfid_spi));
	rc522_init();
	initialize_espnow();

	ESP_ERROR_CHECK(xTaskCreate(console_task, "serial_console", 4096, NULL, 5, NULL) == pdPASS
						? ESP_OK
						: ESP_FAIL);
	ESP_LOGI(TAG, "Terminal ready: room %u (%s), %s; default admin PIN is 1234",
			 (unsigned)SMART_SYSTEM_ROOM_ID, SMART_SYSTEM_ROOM_NAME,
			 SMART_SYSTEM_EXIT_NODE ? "exit" : "entry");

	uint8_t last_uid[4] = {0};
	TickType_t last_card_tick = 0;
	TickType_t last_broadcast_tick = 0;
	while (true) {
		uint8_t uid[4];
		TickType_t now = xTaskGetTickCount();
		if (rc522_read_uid(uid)) {
			if (memcmp(uid, last_uid, sizeof(uid)) != 0 ||
				(TickType_t)(now - last_card_tick) > pdMS_TO_TICKS(2500)) {
				memcpy(last_uid, uid, sizeof(last_uid));
				last_card_tick = now;
				handle_card(uid);
			}
		} else if ((TickType_t)(now - last_card_tick) > pdMS_TO_TICKS(500)) {
			memset(last_uid, 0, sizeof(last_uid));
		}
		if ((TickType_t)(now - last_broadcast_tick) >= pdMS_TO_TICKS(1000)) {
			broadcast_occupancy();
			last_broadcast_tick = now;
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}
