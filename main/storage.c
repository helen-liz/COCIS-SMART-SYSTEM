#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "storage.h"

#define SD_MOUNT_PATH "/sdcard"
#define SD_LOG_PATH SD_MOUNT_PATH "/events.csv"
#define SPIFFS_LOG_PATH "/spiffs/events.csv"
#define SD_PIN_CS GPIO_NUM_4

static const char *TAG = "smart_system_storage";
static nvs_handle_t config_nvs;
static bool spiffs_mounted;
static bool sd_mounted;
static sdmmc_card_t *sd_card;
static SemaphoreHandle_t log_mutex;
static const char *access_log_path = SPIFFS_LOG_PATH;

void storage_init(spi_host_device_t spi_host)
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
	log_mutex = xSemaphoreCreateMutex();
	ESP_ERROR_CHECK(log_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);

	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = spi_host;
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.host_id = spi_host;
	slot_config.gpio_cs = SD_PIN_CS;
	esp_vfs_fat_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 4,
		.allocation_unit_size = 16 * 1024,
	};
	esp_err_t sd_result = esp_vfs_fat_sdspi_mount(SD_MOUNT_PATH, &host, &slot_config,
											 &mount_config, &sd_card);
	if (sd_result == ESP_OK) {
		sd_mounted = true;
		access_log_path = SD_LOG_PATH;
		ESP_LOGI(TAG, "SD card mounted; event log: %s", SD_LOG_PATH);
	} else {
		ESP_LOGW(TAG, "SD card unavailable (%s); trying SPIFFS log storage",
				 esp_err_to_name(sd_result));
	}

	if (!sd_mounted) {
		esp_vfs_spiffs_conf_t spiffs_config = {
			.base_path = "/spiffs",
			.partition_label = NULL,
			.max_files = 4,
			.format_if_mount_failed = true,
		};
		spiffs_mounted = esp_vfs_spiffs_register(&spiffs_config) == ESP_OK;
	}
	if (!sd_mounted && !spiffs_mounted) {
		ESP_LOGW(TAG, "SD and SPIFFS unavailable; access events will be logged to serial only");
	} else if (!sd_mounted) {
		ESP_LOGW(TAG, "Using SPIFFS fallback for access event logs");
	}
}

void storage_load_config(system_config_t *config)
{
	memset(config, 0, sizeof(*config));
	config->schedule_start_minute = 7 * 60;
	config->schedule_end_minute = 22 * 60;
	strcpy(config->admin_pin, "1234");

	size_t blob_size = sizeof(config->allowed_uids);
	nvs_get_blob(config_nvs, "uids", config->allowed_uids, &blob_size);
	nvs_get_u8(config_nvs, "uid_count", &config->allowed_count);
	nvs_get_u16(config_nvs, "start_min", &config->schedule_start_minute);
	nvs_get_u16(config_nvs, "end_min", &config->schedule_end_minute);
	size_t pin_size = sizeof(config->admin_pin);
	nvs_get_str(config_nvs, "admin_pin", config->admin_pin, &pin_size);
	if (config->allowed_count > MAX_ALLOWED_CARDS) {
		config->allowed_count = 0;
	}
	if (config->schedule_start_minute >= 1440 ||
		config->schedule_end_minute >= 1440 ||
		config->schedule_start_minute >= config->schedule_end_minute) {
		config->schedule_start_minute = 7 * 60;
		config->schedule_end_minute = 22 * 60;
	}
}

void storage_save_config(const system_config_t *config)
{
	nvs_set_blob(config_nvs, "uids", config->allowed_uids,
				sizeof(config->allowed_uids));
	nvs_set_u8(config_nvs, "uid_count", config->allowed_count);
	nvs_set_u16(config_nvs, "start_min", config->schedule_start_minute);
	nvs_set_u16(config_nvs, "end_min", config->schedule_end_minute);
	nvs_set_str(config_nvs, "admin_pin", config->admin_pin);
	ESP_ERROR_CHECK(nvs_commit(config_nvs));
}

void storage_load_occupancy(uint8_t room_id, int32_t *occupancy)
{
	char key[8];
	if (room_id > 2 || occupancy == NULL) {
		return;
	}
	snprintf(key, sizeof(key), "occ%u", (unsigned)room_id);
	if (nvs_get_i32(config_nvs, key, occupancy) != ESP_OK) {
		*occupancy = 0;
	}
}

void storage_save_occupancy(uint8_t room_id, int32_t occupancy)
{
	char key[8];
	if (room_id > 2) {
		return;
	}
	snprintf(key, sizeof(key), "occ%u", (unsigned)room_id);
	nvs_set_i32(config_nvs, key, occupancy);
	ESP_ERROR_CHECK(nvs_commit(config_nvs));
}

void storage_write_event(const room_profile_t *profile, const char *event,
						 const uint8_t *uid, const char *detail)
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
			 (unsigned)profile->room_id, profile->name, event, uid_text, detail);
	if (!sd_mounted && !spiffs_mounted) {
		return;
	}
	xSemaphoreTake(log_mutex, portMAX_DELAY);
	FILE *log_file = fopen(access_log_path, "a");
	if (log_file != NULL) {
		fprintf(log_file, "%s,%u,%s,%s,%s,%s\n", timestamp,
				(unsigned)profile->room_id, profile->name, event, uid_text, detail);
		fclose(log_file);
	} else {
		ESP_LOGW(TAG, "Could not append event to %s", access_log_path);
	}
	xSemaphoreGive(log_mutex);
}

void storage_print_logs(void)
{
	if (!sd_mounted && !spiffs_mounted) {
		printf("Local log storage is unavailable.\n");
		return;
	}
	xSemaphoreTake(log_mutex, portMAX_DELAY);
	FILE *log_file = fopen(access_log_path, "r");
	if (log_file == NULL) {
		printf("No events recorded yet.\n");
		xSemaphoreGive(log_mutex);
		return;
	}
	char line[160];
	while (fgets(line, sizeof(line), log_file) != NULL) {
		fputs(line, stdout);
	}
	fclose(log_file);
	xSemaphoreGive(log_mutex);
}