#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage.h"

#define ACCESS_LOG_PATH  "/sdcard/events.csv"
#define SD_CS_PIN        15
#define SD_SPI_HOST      SPI2_HOST

static const char *TAG = "smart_system_storage";
static nvs_handle_t config_nvs;
static bool sd_mounted;
static sdmmc_card_t *sd_card;

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

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4,
		.allocation_unit_size = 16 * 1024,
	};
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = SD_CS_PIN;
	slot_config.host_id = SD_SPI_HOST;
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	esp_err_t sd_result = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config,
												   &mount_config, &sd_card);
	if (sd_result != ESP_OK) {
		ESP_LOGE(TAG, "SD card mount failed (%s); access events will be logged to serial only",
				 esp_err_to_name(sd_result));
	} else {
		sd_mounted = true;
		ESP_LOGI(TAG, "SD card ready (%lluMB)",
				 ((uint64_t)sd_card->csd.capacity) * sd_card->csd.sector_size / (1024 * 1024));
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
	*occupancy = 0;
	nvs_get_i32(config_nvs, "occupancy", occupancy);
}

void storage_save_occupancy(int32_t occupancy)
{
	nvs_set_i32(config_nvs, "occupancy", occupancy);
	ESP_ERROR_CHECK(nvs_commit(config_nvs));
}

void storage_load_inside(inside_record_t *records, uint8_t *count)
{
	memset(records, 0, sizeof(inside_record_t) * MAX_INSIDE_RECORDS);
	*count = 0;
	size_t blob_size = sizeof(inside_record_t) * MAX_INSIDE_RECORDS;
	if (nvs_get_blob(config_nvs, "inside", records, &blob_size) != ESP_OK) {
		return;
	}
	nvs_get_u8(config_nvs, "inside_count", count);
	if (*count > MAX_INSIDE_RECORDS) {
		*count = 0;
	}
}

void storage_save_inside(const inside_record_t *records, uint8_t count)
{
	if (count > MAX_INSIDE_RECORDS) {
		count = MAX_INSIDE_RECORDS;
	}
	nvs_set_blob(config_nvs, "inside", records,
				 sizeof(inside_record_t) * MAX_INSIDE_RECORDS);
	nvs_set_u8(config_nvs, "inside_count", count);
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
	bool new_file = access(ACCESS_LOG_PATH, F_OK) != 0;
	FILE *log_file = fopen(ACCESS_LOG_PATH, "a");
	if (log_file == NULL) {
		ESP_LOGE(TAG, "Unable to open %s for append: %s", ACCESS_LOG_PATH,
				 strerror(errno));
		return;
	}
	if (new_file &&
		fprintf(log_file, "timestamp,room_id,room,event,uid,detail\n") < 0) {
		ESP_LOGE(TAG, "Unable to write CSV header: %s", strerror(errno));
		fclose(log_file);
		return;
	}
	if (fprintf(log_file, "%s,%u,%s,%s,%s,%s\n", timestamp,
				(unsigned)profile->room_id, profile->name, event, uid_text, detail) < 0) {
		ESP_LOGE(TAG, "Unable to write access event: %s", strerror(errno));
		fclose(log_file);
		return;
	}
	if (fflush(log_file) != 0 || fsync(fileno(log_file)) != 0) {
		ESP_LOGE(TAG, "Unable to flush access event: %s", strerror(errno));
	}
	fclose(log_file);
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

size_t storage_read_logs(char *buffer, size_t max_size)
{
	if (buffer == NULL || max_size == 0) {
		return 0;
	}
	buffer[0] = '\0';
	if (!spiffs_mounted) {
		return 0;
	}
	FILE *log_file = fopen(ACCESS_LOG_PATH, "r");
	if (log_file == NULL) {
		return 0;
	}
	if (fseek(log_file, 0, SEEK_END) != 0) {
		fclose(log_file);
		return 0;
	}
	long file_size = ftell(log_file);
	if (file_size <= 0) {
		fclose(log_file);
		return 0;
	}
	size_t total = 0;
	size_t capacity = max_size - 1;
	if ((size_t)file_size > capacity) {
		if (fseek(log_file, 0, SEEK_SET) != 0 ||
			fgets(buffer, (int)max_size, log_file) == NULL) {
			fclose(log_file);
			return 0;
		}
		total = strlen(buffer);
		size_t tail_capacity = capacity - total;
		long start = file_size - (long)tail_capacity;
		if (start < 0) {
			start = 0;
		}
		if (fseek(log_file, start, SEEK_SET) != 0) {
			buffer[total] = '\0';
			fclose(log_file);
			return total;
		}
		int character;
		while ((character = fgetc(log_file)) != EOF && character != '\n') {
		}
	} else if (fseek(log_file, 0, SEEK_SET) != 0) {
		fclose(log_file);
		return 0;
	}
	while (total + 1 < max_size) {
		size_t chunk = fread(buffer + total, 1, max_size - total - 1, log_file);
		if (chunk == 0) {
			break;
		}
		total += chunk;
	}
	buffer[total] = '\0';
	fclose(log_file);
	return total;
}

void storage_clear_logs(void)
{
	if (spiffs_mounted) {
		remove(ACCESS_LOG_PATH);
	}
}

size_t storage_log_size(void)
{
	if (!spiffs_mounted) {
		return 0;
	}
	struct stat info;
	if (stat(ACCESS_LOG_PATH, &info) != 0) {
		return 0;
	}
	return (size_t)info.st_size;
}