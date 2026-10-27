#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage.h"

#define ACCESS_LOG_PATH "/spiffs/events.csv"

static const char *TAG = "smart_system_storage";
static nvs_handle_t config_nvs;
static bool spiffs_mounted;

void storage_init(void)
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

void storage_load_occupancy(int32_t *occupancy)
{
	nvs_get_i32(config_nvs, "occupancy", occupancy);
}

void storage_save_occupancy(int32_t occupancy)
{
	nvs_set_i32(config_nvs, "occupancy", occupancy);
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
	if (!spiffs_mounted) {
		return;
	}
	FILE *log_file = fopen(ACCESS_LOG_PATH, "a");
	if (log_file != NULL) {
		fprintf(log_file, "%s,%u,%s,%s,%s,%s\n", timestamp,
				(unsigned)profile->room_id, profile->name, event, uid_text, detail);
		fclose(log_file);
	}
}

void storage_print_logs(void)
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